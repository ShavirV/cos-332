import java.io.*;
import java.net.*;
import java.util.*;

/**
 * ldap client that communicates with an ldap server on port 389 using raw tcp sockets.
 * implements ldap v3 as defined in rfc 4511, encoding all messages manually using ber/der.
 * supports: bind (simple auth), search, unbind, and basic response parsing.
 *
 * directory information tree assumed:
 *   dc=example,dc=com
 *     ou=Automobiles
 *       cn=Ferrari  (maxSpeed: 320)
 *       cn=Bugatti  (maxSpeed: 431)
 *       ...
 *
 * each asset entry uses objectClass=top,organizationalRole (or a custom class)
 * with a 'description' attribute carrying the max speed value in km/h.
 * alternatively, if a custom schema is loaded, a dedicated 'maxSpeed' attribute is used.
 *
 * usage: java LDAPClient <host> <port> <bindDN> <password> <baseDN> <assetName>
 * example: java LDAPClient localhost 389 "cn=admin,dc=example,dc=com" secret "ou=Automobiles,dc=example,dc=com" Ferrari
 */
public class LDAPClient {

    //ldap version constant (always 3 per rfc 4511)
    private static final int LDAP_VERSION = 3;

    //ber universal tag numbers (class = universal, primitive)
    private static final int TAG_BOOLEAN         = 0x01;
    private static final int TAG_INTEGER         = 0x02;
    private static final int TAG_OCTET_STRING    = 0x04;
    private static final int TAG_ENUMERATED      = 0x0A;
    private static final int TAG_SEQUENCE        = 0x30; //constructed
    private static final int TAG_SET             = 0x31; //constructed

    //ldap application-class tags (class = application, constructed unless noted)
    //per rfc 4511 appendix b
    private static final int TAG_BIND_REQUEST    = 0x60; //app 0, constructed
    private static final int TAG_BIND_RESPONSE   = 0x61; //app 1, constructed
    private static final int TAG_UNBIND_REQUEST  = 0x42; //app 2, primitive
    private static final int TAG_SEARCH_REQUEST  = 0x63; //app 3, constructed
    private static final int TAG_SEARCH_RES_ENTRY= 0x64; //app 4, constructed
    private static final int TAG_SEARCH_RES_DONE = 0x65; //app 5, constructed
    private static final int TAG_SEARCH_RES_REF  = 0x73; //app 19, constructed

    //ldap filter tags (context-specific)
    private static final int TAG_FILTER_AND      = 0xA0; //[0] constructed
    private static final int TAG_FILTER_OR       = 0xA1; //[1] constructed
    private static final int TAG_FILTER_NOT      = 0xA2; //[2] constructed
    private static final int TAG_FILTER_EQUAL    = 0xA3; //[3] constructed (equalityMatch)
    private static final int TAG_FILTER_PRESENT  = 0x87; //[7] primitive (present)

    //ldap result codes (rfc 4511 section 4.1.9)
    private static final int RESULT_SUCCESS      = 0;
    private static final int RESULT_REFERRAL     = 10;

    //the tcp socket and its streams
    private Socket socket;
    private OutputStream out;
    private InputStream in;

    //message id counter; each request gets a unique id
    private int messageId = 0;

    //--------------------------------------------------------------------
    // main entry point
    //--------------------------------------------------------------------
    public static void main(String[] args) throws Exception {
        //parse arguments
        if (args.length < 6) {
            System.err.println("usage: LDAPClient <host> <port> <bindDN> <password> <baseDN> <assetName>");
            System.err.println("example: LDAPClient localhost 389 \"cn=admin,dc=example,dc=com\" secret \"ou=Automobiles,dc=example,dc=com\" Ferrari");
            System.exit(1);
        }
        String host     = args[0];
        int    port     = Integer.parseInt(args[1]);
        String bindDN   = args[2];
        String password = args[3];
        String baseDN   = args[4];
        String assetName= args[5];

        LDAPClient client = new LDAPClient();

        try {
            //step 1: open tcp connection to ldap server
            System.out.println("[*] connecting to " + host + ":" + port);
            client.connect(host, port);
            System.out.println("[*] connected");

            //step 2: send a bind request to authenticate
            //simple bind sends credentials as plaintext (acceptable for this assignment)
            System.out.println("[*] binding as: " + bindDN);
            int bindResult = client.sendBindRequest(bindDN, password);
            if (bindResult != RESULT_SUCCESS) {
                System.err.println("[!] bind failed with result code: " + bindResult
                        + " (" + resultCodeToString(bindResult) + ")");
                client.sendUnbindRequest();
                client.disconnect();
                System.exit(1);
            }
            System.out.println("[*] bind successful");

            //step 3: send a search request for the named asset
            //we search for (cn=<assetName>) under the given base dn
            //requesting the 'description' attribute which we use to store max speed
            System.out.println("[*] searching for asset: " + assetName);
            List<Map<String, List<String>>> results = client.sendSearchRequest(
                    baseDN,
                    assetName,
                    new String[]{"description", "maxSpeed", "cn"}
            );

            //step 4: display results
            if (results.isEmpty()) {
                System.out.println("[!] asset '" + assetName + "' not found in directory.");
            } else {
                for (Map<String, List<String>> entry : results) {
                    System.out.println("\n[+] entry found:");
                    //print cn (common name / asset name)
                    List<String> cnVals = entry.get("cn");
                    if (cnVals != null) {
                        System.out.println("    name       : " + String.join(", ", cnVals));
                    }
                    //try to read maxSpeed attribute first (custom schema)
                    List<String> speedVals = entry.get("maxSpeed");
                    if (speedVals == null || speedVals.isEmpty()) {
                        //fall back to description attribute
                        speedVals = entry.get("description");
                    }
                    if (speedVals != null && !speedVals.isEmpty()) {
                        System.out.println("    max speed  : " + speedVals.get(0) + " km/h");
                    } else {
                        System.out.println("    max speed  : (attribute not found)");
                    }
                    //print all other attributes for informational purposes
                    for (Map.Entry<String, List<String>> attr : entry.entrySet()) {
                        String key = attr.getKey();
                        if (!key.equalsIgnoreCase("cn") &&
                            !key.equalsIgnoreCase("maxSpeed") &&
                            !key.equalsIgnoreCase("description")) {
                            System.out.println("    " + key + " : " + String.join(", ", attr.getValue()));
                        }
                    }
                }
            }

            //step 5: send unbind to cleanly close the session per rfc 4511 section 4.3
            System.out.println("\n[*] unbinding and closing connection");
            client.sendUnbindRequest();

        } finally {
            client.disconnect();
        }
    }

    //--------------------------------------------------------------------
    // connection management
    //--------------------------------------------------------------------

    //opens a plain tcp socket to the ldap server
    public void connect(String host, int port) throws IOException {
        socket = new Socket(host, port);
        socket.setSoTimeout(10000); //10 second read timeout
        out = socket.getOutputStream();
        in  = socket.getInputStream();
    }

    //closes the socket
    public void disconnect() {
        try {
            if (socket != null && !socket.isClosed()) socket.close();
        } catch (IOException ignored) {}
    }

    //--------------------------------------------------------------------
    // ldap bind (rfc 4511 section 4.2)
    //--------------------------------------------------------------------

    /**
     * sends a simple bind request and returns the ldap result code.
     * simple authentication sends the password in cleartext.
     * the bind request message structure per rfc 4511:
     *   BindRequest ::= [APPLICATION 0] SEQUENCE {
     *     version   INTEGER (1..127),
     *     name      LDAPDN,
     *     authentication  AuthenticationChoice
     *   }
     *   AuthenticationChoice ::= CHOICE {
     *     simple  [0] OCTET STRING,  -- 0-based context tag, primitive
     *     ...
     *   }
     */
    public int sendBindRequest(String dn, String password) throws IOException {
        int msgId = ++messageId;

        //build the bind request payload
        ByteArrayOutputStream payload = new ByteArrayOutputStream();
        //version = 3
        appendTLV(payload, TAG_INTEGER, encodeInteger(LDAP_VERSION));
        //name = bind dn as octet string
        appendTLV(payload, TAG_OCTET_STRING, dn.getBytes("UTF-8"));
        //simple authentication: context tag [0] primitive with password bytes
        appendTLV(payload, 0x80, password.getBytes("UTF-8")); //[0] primitive

        //wrap payload in bind request application tag
        byte[] bindReq = wrapTLV(TAG_BIND_REQUEST, payload.toByteArray());

        //wrap in ldap message envelope
        byte[] ldapMsg = buildLDAPMessage(msgId, bindReq);

        //send to server
        out.write(ldapMsg);
        out.flush();
        System.out.println("[*] bind request sent (" + ldapMsg.length + " bytes, msgId=" + msgId + ")");

        //read and parse the bind response
        return readBindResponse(msgId);
    }

    //reads a bind response and extracts the result code
    private int readBindResponse(int expectedMsgId) throws IOException {
        byte[] response = readLDAPMessage();
        if (response == null) throw new IOException("no response received from server");

        //parse outer ldap message sequence
        BerParser msg = new BerParser(response);
        msg.expectTag(TAG_SEQUENCE);
        byte[] msgContent = msg.readLength();

        BerParser msgParser = new BerParser(msgContent);
        //message id
        msgParser.expectTag(TAG_INTEGER);
        int respMsgId = decodeInteger(msgParser.readLength());
        if (respMsgId != expectedMsgId) {
            throw new IOException("message id mismatch: expected " + expectedMsgId + ", got " + respMsgId);
        }

        //bind response application tag
        msgParser.expectTag(TAG_BIND_RESPONSE);
        byte[] bindRespContent = msgParser.readLength();

        //parse ldapresult inside bind response
        return parseLDAPResult(bindRespContent);
    }

    //--------------------------------------------------------------------
    // ldap unbind (rfc 4511 section 4.3)
    //--------------------------------------------------------------------

    /**
     * sends an unbind request to the server.
     * the unbind request is a primitive (not a sequence) with no content.
     * it signals that the client is done and will close the connection.
     *   UnbindRequest ::= [APPLICATION 2] NULL
     */
    public void sendUnbindRequest() throws IOException {
        int msgId = ++messageId;
        //unbind request has null body
        byte[] unbindReq = wrapTLV(TAG_UNBIND_REQUEST, new byte[0]);
        byte[] ldapMsg   = buildLDAPMessage(msgId, unbindReq);
        out.write(ldapMsg);
        out.flush();
        System.out.println("[*] unbind request sent (msgId=" + msgId + ")");
        //no response is expected after unbind
    }

    //--------------------------------------------------------------------
    // ldap search (rfc 4511 section 4.5)
    //--------------------------------------------------------------------

    /**
     * sends a search request and collects all searchresultentry messages.
     * search request structure per rfc 4511:
     *   SearchRequest ::= [APPLICATION 3] SEQUENCE {
     *     baseObject   LDAPDN,
     *     scope        ENUMERATED { baseObject(0), singleLevel(1), wholeSubtree(2) },
     *     derefAliases ENUMERATED { neverDerefAliases(0), ... },
     *     sizeLimit    INTEGER,
     *     timeLimit    INTEGER,
     *     typesOnly    BOOLEAN,
     *     filter       Filter,
     *     attributes   AttributeSelection
     *   }
     *
     * we construct an equalityMatch filter: (cn=<assetName>)
     * and request specific attributes by name.
     */
    public List<Map<String, List<String>>> sendSearchRequest(
            String baseDN, String assetName, String[] requestedAttrs) throws IOException {

        int msgId = ++messageId;

        ByteArrayOutputStream payload = new ByteArrayOutputStream();

        //base object dn
        appendTLV(payload, TAG_OCTET_STRING, baseDN.getBytes("UTF-8"));

        //scope: wholeSubtree(2) to search all levels under baseDN
        appendTLV(payload, TAG_ENUMERATED, encodeInteger(2));

        //derefAliases: neverDerefAliases(0)
        appendTLV(payload, TAG_ENUMERATED, encodeInteger(0));

        //sizeLimit: 0 = no limit
        appendTLV(payload, TAG_INTEGER, encodeInteger(0));

        //timeLimit: 0 = no limit
        appendTLV(payload, TAG_INTEGER, encodeInteger(0));

        //typesOnly: false (we want values, not just attribute type names)
        appendTLV(payload, TAG_BOOLEAN, new byte[]{0x00});

        //filter: equalityMatch [3] SEQUENCE { attributeDesc, assertionValue }
        //this builds the ber for (cn=<assetName>)
        byte[] filterBytes = buildEqualityFilter("cn", assetName);
        payload.write(filterBytes);

        //attributes: sequence of octet strings naming desired attributes
        ByteArrayOutputStream attrList = new ByteArrayOutputStream();
        for (String attr : requestedAttrs) {
            appendTLV(attrList, TAG_OCTET_STRING, attr.getBytes("UTF-8"));
        }
        appendTLV(payload, TAG_SEQUENCE, attrList.toByteArray());

        //wrap in search request application tag
        byte[] searchReq = wrapTLV(TAG_SEARCH_REQUEST, payload.toByteArray());
        byte[] ldapMsg   = buildLDAPMessage(msgId, searchReq);

        out.write(ldapMsg);
        out.flush();
        System.out.println("[*] search request sent (" + ldapMsg.length + " bytes, msgId=" + msgId + ")");

        //collect all search result entries until searchresultdone
        return readSearchResults(msgId);
    }

    //reads search result entries and the final searchresultdone message
    private List<Map<String, List<String>>> readSearchResults(int expectedMsgId) throws IOException {
        List<Map<String, List<String>>> entries = new ArrayList<>();

        while (true) {
            byte[] response = readLDAPMessage();
            if (response == null) throw new IOException("server closed connection during search");

            BerParser msg = new BerParser(response);
            msg.expectTag(TAG_SEQUENCE);
            byte[] msgContent = msg.readLength();

            BerParser msgParser = new BerParser(msgContent);

            //message id
            msgParser.expectTag(TAG_INTEGER);
            int respMsgId = decodeInteger(msgParser.readLength());
            if (respMsgId != expectedMsgId) {
                //could be an unsolicited notification; skip
                System.err.println("[!] unexpected msgId " + respMsgId + " (expected " + expectedMsgId + "), skipping");
                continue;
            }

            //peek at the next tag to determine message type
            int tag = msgParser.peekTag();

            if (tag == TAG_SEARCH_RES_ENTRY) {
                //parse a search result entry
                msgParser.expectTag(TAG_SEARCH_RES_ENTRY);
                byte[] entryContent = msgParser.readLength();
                Map<String, List<String>> entry = parseSearchResultEntry(entryContent);
                entries.add(entry);

            } else if (tag == TAG_SEARCH_RES_DONE) {
                //search is complete
                msgParser.expectTag(TAG_SEARCH_RES_DONE);
                byte[] doneContent = msgParser.readLength();
                int resultCode = parseLDAPResult(doneContent);
                if (resultCode != RESULT_SUCCESS && resultCode != 0) {
                    System.err.println("[!] search result code: " + resultCode
                            + " (" + resultCodeToString(resultCode) + ")");
                }
                System.out.println("[*] search complete, " + entries.size() + " entry/entries returned");
                break;

            } else if (tag == TAG_SEARCH_RES_REF) {
                //referrals (rfc 4511 section 4.5.3) — we log but do not follow
                msgParser.expectTag(TAG_SEARCH_RES_REF);
                byte[] refContent = msgParser.readLength();
                System.out.println("[*] referral received (not followed): " + new String(refContent, "UTF-8"));

            } else {
                //unknown or unexpected protocol data unit
                System.err.println("[!] unexpected tag 0x" + Integer.toHexString(tag) + " in search response");
                break;
            }
        }
        return entries;
    }

    /**
     * parses a searchresultentry pdu.
     * structure (rfc 4511 section 4.5.2):
     *   SearchResultEntry ::= [APPLICATION 4] SEQUENCE {
     *     objectName LDAPDN,
     *     attributes PartialAttributeList
     *   }
     *   PartialAttributeList ::= SEQUENCE OF PartialAttribute
     *   PartialAttribute ::= SEQUENCE {
     *     type   AttributeDescription,
     *     vals   SET OF AttributeValue
     *   }
     */
    private Map<String, List<String>> parseSearchResultEntry(byte[] content) throws IOException {
        Map<String, List<String>> entry = new LinkedHashMap<>();

        BerParser p = new BerParser(content);

        //object name (the dn of the entry)
        p.expectTag(TAG_OCTET_STRING);
        String dn = new String(p.readLength(), "UTF-8");
        entry.put("dn", Collections.singletonList(dn));

        //attributes sequence
        p.expectTag(TAG_SEQUENCE);
        byte[] attrsContent = p.readLength();
        BerParser attrsParser = new BerParser(attrsContent);

        //iterate over partial attributes
        while (attrsParser.hasMore()) {
            attrsParser.expectTag(TAG_SEQUENCE);
            byte[] attrContent = attrsParser.readLength();
            BerParser attrParser = new BerParser(attrContent);

            //attribute type (name)
            attrParser.expectTag(TAG_OCTET_STRING);
            String attrType = new String(attrParser.readLength(), "UTF-8");

            //attribute values (a set)
            attrParser.expectTag(TAG_SET);
            byte[] valsContent = attrParser.readLength();
            BerParser valsParser = new BerParser(valsContent);

            List<String> values = new ArrayList<>();
            while (valsParser.hasMore()) {
                valsParser.expectTag(TAG_OCTET_STRING);
                String val = new String(valsParser.readLength(), "UTF-8");
                values.add(val);
            }
            entry.put(attrType.toLowerCase(), values);
        }
        return entry;
    }

    //--------------------------------------------------------------------
    // ldap result code parsing (rfc 4511 section 4.1.9)
    //--------------------------------------------------------------------

    /**
     * parses an ldapresult structure:
     *   LDAPResult ::= SEQUENCE {
     *     resultCode    ENUMERATED { ... },
     *     matchedDN     LDAPDN,
     *     diagnosticMessage LDAPString,
     *     referral      [3] Referral OPTIONAL
     *   }
     */
    private int parseLDAPResult(byte[] content) throws IOException {
        BerParser p = new BerParser(content);

        //result code
        p.expectTag(TAG_ENUMERATED);
        int resultCode = decodeInteger(p.readLength());

        //matched dn
        p.expectTag(TAG_OCTET_STRING);
        String matchedDN = new String(p.readLength(), "UTF-8");

        //diagnostic message
        p.expectTag(TAG_OCTET_STRING);
        String diagMsg = new String(p.readLength(), "UTF-8");

        if (resultCode != RESULT_SUCCESS) {
            System.err.println("[!] ldap result code=" + resultCode
                    + " matchedDN='" + matchedDN + "' msg='" + diagMsg + "'");
        }
        return resultCode;
    }

    //--------------------------------------------------------------------
    // ber/der filter construction helpers
    //--------------------------------------------------------------------

    /**
     * builds an equalityMatch filter ber encoding.
     * equalityMatch [APPLICATION 3] SEQUENCE {
     *   attributeDesc  OCTET STRING,
     *   assertionValue OCTET STRING
     * }
     * rfc 4511 section 4.5.1 defines the filter choices.
     */
    private byte[] buildEqualityFilter(String attrDesc, String assertionValue) throws IOException {
        ByteArrayOutputStream seq = new ByteArrayOutputStream();
        appendTLV(seq, TAG_OCTET_STRING, attrDesc.getBytes("UTF-8"));
        appendTLV(seq, TAG_OCTET_STRING, assertionValue.getBytes("UTF-8"));
        return wrapTLV(TAG_FILTER_EQUAL, seq.toByteArray());
    }

    /**
     * builds a present filter: checks that an attribute exists.
     * present [7] IMPLICIT OCTET STRING  (primitive, context class)
     * useful for checking e.g. (cn=*)
     */
    @SuppressWarnings("unused")
    private byte[] buildPresentFilter(String attrDesc) throws IOException {
        return wrapTLV(TAG_FILTER_PRESENT, attrDesc.getBytes("UTF-8"));
    }

    /**
     * builds an and filter combining two sub-filters.
     * and [0] SET OF Filter
     */
    @SuppressWarnings("unused")
    private byte[] buildAndFilter(byte[]... subFilters) throws IOException {
        ByteArrayOutputStream set = new ByteArrayOutputStream();
        for (byte[] f : subFilters) set.write(f);
        return wrapTLV(TAG_FILTER_AND, set.toByteArray());
    }

    //--------------------------------------------------------------------
    // ldap message framing (rfc 4511 section 4.1.1)
    //--------------------------------------------------------------------

    /**
     * wraps a request pdu into a complete ldap message sequence.
     *   LDAPMessage ::= SEQUENCE {
     *     messageID  MessageID,
     *     protocolOp CHOICE { ... },
     *     controls   [0] Controls OPTIONAL
     *   }
     * controls are omitted for simplicity but the rfc allows them for
     * extensions like paged results, sorting, etc.
     */
    private byte[] buildLDAPMessage(int msgId, byte[] protocolOp) throws IOException {
        ByteArrayOutputStream content = new ByteArrayOutputStream();
        appendTLV(content, TAG_INTEGER, encodeInteger(msgId));
        content.write(protocolOp);
        return wrapTLV(TAG_SEQUENCE, content.toByteArray());
    }

    //--------------------------------------------------------------------
    // raw tcp read helpers
    //--------------------------------------------------------------------

    /**
     * reads exactly one complete ldap message from the input stream.
     * ldap messages are ber-encoded sequences; we read the tag and length
     * first, then read exactly that many value bytes.
     * this is critical: we must not over-read or under-read.
     */
    private byte[] readLDAPMessage() throws IOException {
        //read the outer tag (should always be 0x30 = SEQUENCE for ldap messages)
        int tagByte = in.read();
        if (tagByte == -1) return null; //connection closed
        if ((tagByte & 0x1F) == 0x1F) {
            //long-form tag (multi-byte); not used by ldap but handled defensively
            throw new IOException("long-form tags not supported (tag=0x" + Integer.toHexString(tagByte) + ")");
        }

        //read the length octets (ber definite form)
        int firstLenByte = in.read();
        if (firstLenByte == -1) throw new EOFException("eof reading length");
        int totalLength;
        if ((firstLenByte & 0x80) == 0) {
            //short form: length is in bits 0-6
            totalLength = firstLenByte;
        } else {
            //long form: bits 0-6 give the number of subsequent length octets
            int numLenBytes = firstLenByte & 0x7F;
            if (numLenBytes == 0) throw new IOException("indefinite-length ber not supported");
            if (numLenBytes > 4) throw new IOException("length too large (" + numLenBytes + " bytes)");
            totalLength = 0;
            for (int i = 0; i < numLenBytes; i++) {
                int b = in.read();
                if (b == -1) throw new EOFException("eof in length octets");
                totalLength = (totalLength << 8) | b;
            }
        }

        //read exactly totalLength bytes of value
        byte[] value = new byte[totalLength];
        int read = 0;
        while (read < totalLength) {
            int n = in.read(value, read, totalLength - read);
            if (n == -1) throw new EOFException("eof reading ldap message value");
            read += n;
        }

        //reassemble the full ber-encoded message (tag + length + value)
        //so we can pass it to the parser cleanly
        ByteArrayOutputStream full = new ByteArrayOutputStream();
        full.write(tagByte);
        encodeLength(full, totalLength);
        full.write(value);
        return full.toByteArray();
    }

    //--------------------------------------------------------------------
    // ber/der encoding utilities
    //--------------------------------------------------------------------

    //appends a tlv (tag-length-value) triple to the given stream
    private void appendTLV(ByteArrayOutputStream out, int tag, byte[] value) throws IOException {
        out.write(tag);
        encodeLength(out, value.length);
        out.write(value);
    }

    //wraps value bytes in a tlv and returns the result as a byte array
    private byte[] wrapTLV(int tag, byte[] value) throws IOException {
        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        appendTLV(buf, tag, value);
        return buf.toByteArray();
    }

    //encodes a ber/der length in definite form
    //short form for lengths 0-127, long form otherwise
    private void encodeLength(OutputStream out, int length) throws IOException {
        if (length < 0x80) {
            out.write(length);
        } else if (length <= 0xFF) {
            out.write(0x81);
            out.write(length);
        } else if (length <= 0xFFFF) {
            out.write(0x82);
            out.write((length >> 8) & 0xFF);
            out.write(length & 0xFF);
        } else {
            out.write(0x83);
            out.write((length >> 16) & 0xFF);
            out.write((length >> 8) & 0xFF);
            out.write(length & 0xFF);
        }
    }

    //encodes a non-negative integer as ber bytes (big-endian, minimal encoding)
    private byte[] encodeInteger(int value) {
        if (value == 0) return new byte[]{0x00};
        //determine number of bytes needed
        int numBytes = 0;
        int tmp = value;
        while (tmp != 0) { tmp >>= 8; numBytes++; }
        byte[] result = new byte[numBytes];
        for (int i = numBytes - 1; i >= 0; i--) {
            result[i] = (byte)(value & 0xFF);
            value >>= 8;
        }
        //ber signed integer: if high bit of first byte is set, prepend 0x00 to avoid sign confusion
        if ((result[0] & 0x80) != 0) {
            byte[] padded = new byte[result.length + 1];
            padded[0] = 0x00;
            System.arraycopy(result, 0, padded, 1, result.length);
            return padded;
        }
        return result;
    }

    //decodes a ber integer from a byte array
    private int decodeInteger(byte[] bytes) {
        int value = 0;
        for (byte b : bytes) {
            value = (value << 8) | (b & 0xFF);
        }
        return value;
    }

    //--------------------------------------------------------------------
    // ldap result code to human-readable string (rfc 4511 appendix a)
    //--------------------------------------------------------------------
    private static String resultCodeToString(int code) {
        switch (code) {
            case 0:  return "success";
            case 1:  return "operationsError";
            case 2:  return "protocolError";
            case 3:  return "timeLimitExceeded";
            case 4:  return "sizeLimitExceeded";
            case 7:  return "authMethodNotSupported";
            case 8:  return "strongerAuthRequired";
            case 10: return "referral";
            case 11: return "adminLimitExceeded";
            case 16: return "noSuchAttribute";
            case 17: return "undefinedAttributeType";
            case 32: return "noSuchObject";
            case 33: return "aliasDereferencingProblem";
            case 34: return "invalidDNSyntax";
            case 48: return "inappropriateAuthentication";
            case 49: return "invalidCredentials";
            case 50: return "insufficientAccessRights";
            case 51: return "busy";
            case 52: return "unavailable";
            case 53: return "unwillingToPerform";
            case 64: return "namingViolation";
            case 65: return "objectClassViolation";
            case 66: return "notAllowedOnNonLeaf";
            case 80: return "other";
            default: return "unknown(" + code + ")";
        }
    }

    //--------------------------------------------------------------------
    // inner class: ber parser for reading response bytes
    //--------------------------------------------------------------------

    /**
     * stateful ber byte-stream parser.
     * tracks current position and provides methods to expect tags and read
     * length-prefixed value bytes, matching the ber structure defined in
     * itu-t x.690 (referenced by ldap via rfc 4511).
     */
    static class BerParser {
        private final byte[] data;
        private int pos;

        BerParser(byte[] data) {
            this.data = data;
            this.pos  = 0;
        }

        //returns true if there is at least one unread byte
        boolean hasMore() {
            return pos < data.length;
        }

        //peeks at the current tag byte without consuming it
        int peekTag() throws IOException {
            if (pos >= data.length) throw new EOFException("eof peeking tag");
            return data[pos] & 0xFF;
        }

        //reads and asserts the next tag byte equals the expected tag
        void expectTag(int expectedTag) throws IOException {
            if (pos >= data.length) throw new EOFException("eof reading tag");
            int actual = data[pos++] & 0xFF;
            if (actual != expectedTag) {
                throw new IOException("ber tag mismatch: expected 0x"
                        + Integer.toHexString(expectedTag)
                        + " but got 0x" + Integer.toHexString(actual)
                        + " at offset " + (pos - 1));
            }
        }

        //reads the ber length and then reads and returns exactly that many value bytes
        byte[] readLength() throws IOException {
            if (pos >= data.length) throw new EOFException("eof reading length");
            int firstByte = data[pos++] & 0xFF;
            int length;
            if ((firstByte & 0x80) == 0) {
                length = firstByte;
            } else {
                int numBytes = firstByte & 0x7F;
                if (numBytes == 0) throw new IOException("indefinite-length ber not supported");
                length = 0;
                for (int i = 0; i < numBytes; i++) {
                    if (pos >= data.length) throw new EOFException("eof in multi-byte length");
                    length = (length << 8) | (data[pos++] & 0xFF);
                }
            }
            if (pos + length > data.length) {
                throw new IOException("ber value extends beyond buffer: need " + length
                        + " but only " + (data.length - pos) + " bytes remain");
            }
            byte[] value = Arrays.copyOfRange(data, pos, pos + length);
            pos += length;
            return value;
        }
    }
}