#!/bin/bash

set -e

echo "Prac 7"

JAVA_FILE="VacationResponder.java"
CLASS_NAME="VacationResponder"

SMTP_PORT=3025
POP3_PORT=3110

TEST_SENDER1="alice@localhost"
TEST_SENDER2="bob@localhost"
TARGET="me@localhost"

echo "Starting GreenMail..."

# Ensure jar exists
if [ ! -f greenmail-standalone-2.0.0.jar ]; then
    echo "Downloading GreenMail..."
    wget https://repo1.maven.org/maven2/com/icegreen/greenmail-standalone/2.0.0/greenmail-standalone-2.0.0.jar
fi

# Start GreenMail (visible output for debugging)
java -Dgreenmail.setup.test.smtp \
     -Dgreenmail.setup.test.pop3 \
     -Dgreenmail.users=me@localhost:password \
     -jar greenmail-standalone-2.0.0.jar &
GM_PID=$!

# ---- WAIT FOR POP3 PORT ----
echo "Waiting for POP3 server on port $POP3_PORT..."

for i in {1..10}; do
    if (echo > /dev/tcp/localhost/$POP3_PORT) >/dev/null 2>&1; then
        echo "POP3 is ready."
        break
    fi
    echo "Waiting... ($i)"
    sleep 1
done

# Final check
if ! (echo > /dev/tcp/localhost/$POP3_PORT) >/dev/null 2>&1; then
    echo "ERROR: POP3 server never started"
    kill $GM_PID || true
    exit 1
fi

echo "Compiling Java..."
javac $JAVA_FILE

echo "Sending test emails..."

send_mail() {
    {
        echo "HELO localhost"
        echo "MAIL FROM:<$1>"
        echo "RCPT TO:<$TARGET>"
        echo "DATA"
        echo "Subject: $2"
        echo "From: $1"
        echo ""
        echo "Test message"
        echo "."
        echo "QUIT"
    } > /dev/tcp/localhost/$SMTP_PORT
}

# Test cases
send_mail "$TEST_SENDER1" "prac7"
send_mail "$TEST_SENDER1" "prac7"
send_mail "$TEST_SENDER2" "hello"

# Mailing list case
{
    echo "HELO localhost"
    echo "MAIL FROM:<list@localhost>"
    echo "RCPT TO:<$TARGET>"
    echo "DATA"
    echo "Subject: prac7"
    echo "From: list@localhost"
    echo "Precedence: bulk"
    echo ""
    echo "Mailing list message"
    echo "."
    echo "QUIT"
} > /dev/tcp/localhost/$SMTP_PORT

echo "Running vacation responder..."

# Run responder but reduce spam
java $CLASS_NAME 2>&1 | awk '!seen[$0]++' &
RESPONDER_PID=$!

# Let it run
sleep 10

echo "Stopping processes..."
kill $RESPONDER_PID || true
kill $GM_PID || true

echo "Done."