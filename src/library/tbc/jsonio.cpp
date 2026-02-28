/************************************************************************

    jsonio.cpp

    ld-decode-tools TBC library
    Copyright (C) 2022 Adam Sampson

    This file is part of ld-decode-tools.

    ld-decode-tools is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#include "jsonio.h"

#include <limits>
#if (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L) || (defined(__GNUC__) && __GNUC__ >= 11 && __cplusplus >= 201703L)
#define USE_CHARCONV
#endif
#ifdef USE_CHARCONV
#include <charconv>
#else
#include <sstream>
#endif

// Recognise JSON space characters
static bool isAsciiSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Recognise JSON digit characters
static bool isAsciiDigit(char c)
{
    return c >= '0' && c <= '9';
}

JsonReader::JsonReader(std::istream &_input)
    : input(_input), position(0), atStart(true)
{
}

void JsonReader::read(int &value)
{
    readSignedInteger(value);
}

void JsonReader::read(qint64 &value)
{
    readSignedInteger(value);
}

void JsonReader::read(double &value)
{
    readNumber(value);
}

void JsonReader::read(bool &value)
{
    const char *rest;

    char c = spaceGet();
    if (c == 't') {
        rest = "rue";
    } else if (c == 'f') {
        rest = "alse";
    } else {
        throwError("expected true or false");
    }

    while (*rest) {
        if (get() != *rest++) throwError("expected true or false");
    }

    value = (c == 't');
}

void JsonReader::read(std::string &value)
{
    readString(value);
}

void JsonReader::read(QString &value)
{
    readString(buf);
    value = QString::fromUtf8(buf.c_str());
}

void JsonReader::beginObject()
{
    if (spaceGet() != '{') throwError("expected {");

    atStarts.push(atStart);
    atStart = true;
}

bool JsonReader::readMember(std::string &member)
{
    char c = spaceGet();
    if (c == '}') {
        // Found the end - don't consume the }
        unget();
        return false;
    }
    if (atStart) {
        // Start of the first member - don't consume it
        unget();
    } else {
        // Consume the ,
        if (c != ',') throwError("expected , or }");
    }

    readString(member);

    if (spaceGet() != ':') throwError("expected :");

    atStart = false;
    return true;
}

void JsonReader::endObject()
{
    if (spaceGet() != '}') throwError("expected }");

    atStart = atStarts.top();
    atStarts.pop();
}

void JsonReader::beginArray()
{
    if (spaceGet() != '[') throwError("expected [");

    atStarts.push(atStart);
    atStart = true;
}

bool JsonReader::readElement()
{
    char c = spaceGet();
    if (c == ']') {
        // Found the end - don't consume the ]
        unget();
        return false;
    }
    if (atStart) {
        // Start of the first element - don't consume it
        unget();
    } else {
        // Consume the ,
        if (c != ',') throwError("expected , or ]");
    }

    atStart = false;
    return true;
}

void JsonReader::endArray()
{
    if (spaceGet() != ']') throwError("expected ]");

    atStart = atStarts.top();
    atStarts.pop();
}

void JsonReader::discard()
{
    // Peek at the first character to see what type it is
    char c = spaceGet();
    unget();

    if (c == '-' || isAsciiDigit(c)) {
        double dummy;
        readNumber(dummy);
    } else if (c == 't' || c == 'f') {
        bool dummy;
        read(dummy);
    } else if (c == '"') {
        readString(buf);
    } else if (c == '{') {
        beginObject();
        while (readMember(buf)) discard();
        endObject();
    } else if (c == '[') {
        beginArray();
        while (readElement()) discard();
        endArray();
    } else {
        // XXX recognise null
        throwError("unrecognised value");
    }
}

// Get the next input character, returning 0 on EOF or error
char JsonReader::get()
{
    char c;
    input.get(c);
    if (!input.good()) return 0;
    ++position;
    return c;
}

// Get the next input character, discarding spaces before it
char JsonReader::spaceGet()
{
    char c;
    do {
        c = get();
    } while (isAsciiSpace(c));
    return c;
}

// Put back an input character to be read again
void JsonReader::unget()
{
    // We assume the input stream supports unget.
    // (If this becomes a problem in the future, we could simulate it.)
    input.unget();
    --position;
}

// Read a JSON string. The result is unescaped and doesn't include the quotes.
void JsonReader::readString(std::string &value)
{
    char c = spaceGet();
    if (c != '"') throwError("expected string");

    value.clear();

    while (true) {
        c = get();
        switch (c) {
        case 0:
            // End of input
            throwError("end of input in string");
        case '"':
            // End of string
            return;
        case '\\':
            c = get();
            switch (c) {
            case '"':
            case '/':
            case '\\':
                value.push_back(c);
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'u': {
                // Convert \uNNNN into UTF-8.
                // The JSON input string is required to be valid Unicode,
                // so we can just treat NNNN as a 16-bit codepoint.
                // (We don't support surrogate pairs here.)
                unsigned int codepoint = 0;
                for (int i = 0; i < 4; ++i) {
                    c = get();
                    if (c >= '0' && c <= '9') {
                        codepoint = (codepoint << 4) | (c - '0');
                    } else if (c >= 'a' && c <= 'f') {
                        codepoint = (codepoint << 4) | (c - 'a' + 10);
                    } else if (c >= 'A' && c <= 'F') {
                        codepoint = (codepoint << 4) | (c - 'A' + 10);
                    } else {
                        throwError("expected hex digit");
                    }
                }
                if (codepoint <= 0x7F) {
                    value.push_back(static_cast<char>(codepoint));
                } else if (codepoint <= 0x7FF) {
                    value.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                    value.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                } else {
                    value.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                    value.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                    value.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                }
                break;
            }
            default:
                throwError("unrecognised escape in string");
            }
            break;
        default:
            value.push_back(c);
        }
    }
}

// Read a JSON number
void JsonReader::readNumber(double &value)
{
    // Collect the number into a string
    std::string token;
    char c = spaceGet();
    if (c == '-' || isAsciiDigit(c)) {
        token.push_back(c);
    } else {
        throwError("expected number");
    }
    while (true) {
        c = get();
        if (isAsciiDigit(c) || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            token.push_back(c);
        } else {
            unget();
            break;
        }
    }

    // Parse the string
#ifdef USE_CHARCONV
    auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec != std::errc() || ptr != token.data() + token.size()) {
        throwError("invalid number");
    }
#else
    std::stringstream ss(token);
    ss >> value;
    if (ss.fail() || !ss.eof()) {
        throwError("invalid number");
    }
#endif
}

JsonWriter::JsonWriter(std::ostream &_output)
    : output(_output), atStart(true)
{
}

void JsonWriter::write(int value)
{
    output << value;
}

void JsonWriter::write(qint64 value)
{
    output << value;
}

void JsonWriter::write(double value)
{
    if (!std::isfinite(value)) {
        output << "null";
        return;
    }
    output << value;
}

void JsonWriter::write(bool value)
{
    output << (value ? "true" : "false");
}

void JsonWriter::write(const char *value)
{
    writeString(value);
}

void JsonWriter::write(const QString &value)
{
    writeString(value.toUtf8().constData());
}

void JsonWriter::beginObject()
{
    output << '{';
    atStarts.push(atStart);
    atStart = true;
}

void JsonWriter::writeMember(const char *name)
{
    if (atStart) {
        atStart = false;
    } else {
        output << ',';
    }
    writeString(name);
    output << ':';
}

void JsonWriter::endObject()
{
    output << '}';
    atStart = atStarts.top();
    atStarts.pop();
}

void JsonWriter::beginArray()
{
    output << '[';
    atStarts.push(atStart);
    atStart = true;
}

void JsonWriter::writeElement()
{
    if (atStart) {
        atStart = false;
    } else {
        output << ',';
    }
}

void JsonWriter::endArray()
{
    output << ']';
    atStart = atStarts.top();
    atStarts.pop();
}

// Write a JSON string with escaping
void JsonWriter::writeString(const char *str)
{
    output << '"';
    while (*str) {
        unsigned char c = *str++;
        switch (c) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (c < 0x20) {
                // Control characters must be escaped
                output << "\\u";
                output << "00";
                const char hex[] = "0123456789abcdef";
                output << hex[c >> 4] << hex[c & 0x0F];
            } else {
                output << c;
            }
            break;
        }
    }
    output << '"';
}
