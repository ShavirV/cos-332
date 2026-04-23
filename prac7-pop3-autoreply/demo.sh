#!/bin/bash

set -e

echo "Prac 7"

JAVA_FILE="VacationResponder.java"
CLASS_NAME="VacationResponder"

# FIX: correct GreenMail ports
SMTP_PORT=3025
POP3_PORT=3110

TEST_SENDER1="alice@localhost"
TEST_SENDER2="bob@localhost"
TARGET="me@localhost"

echo "Starting GreenMail"

# FIX 1: ensure jar exists
if [ ! -f greenmail-standalone-2.0.0.jar ]; then
    echo "Downloading GreenMail..."
    wget https://repo1.maven.org/maven2/com/icegreen/greenmail-standalone/2.0.0/greenmail-standalone-2.0.0.jar
fi

# FIX 2: background process + correct JVM flags placement
java -Dgreenmail.setup.test.all \
     -Dgreenmail.users=me@localhost:password \
     -jar greenmail-standalone-2.0.0.jar > /dev/null 2>&1 &

GM_PID=$!
sleep 3

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

# valid mail
send_mail "$TEST_SENDER1" "prac7"

# duplicate sender
send_mail "$TEST_SENDER1" "prac7"

# wrong subject
send_mail "$TEST_SENDER2" "hello"

# mailing list
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

echo "running vacation responder"

java $CLASS_NAME &
RESPONDER_PID=$!

sleep 10

kill $RESPONDER_PID || true
kill $GM_PID || true

echo "Everything ran"