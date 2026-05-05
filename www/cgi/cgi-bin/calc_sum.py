#!/usr/bin/env python3

import os
import sys
from urllib.parse import parse_qs


def read_params():
    method = os.environ.get("REQUEST_METHOD", "GET").upper()

    if method == "GET":
        data = os.environ.get("QUERY_STRING", "")
    elif method == "POST":
        length = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
        data = sys.stdin.read(length)
    else:
        return {}

    return parse_qs(data)


def main():
    params = read_params()

    try:
        a = float(params.get("a", ["0"])[0])
        b = float(params.get("b", ["0"])[0])
        result = a + b

        print("Content-Type: text/html")
        print()
        print(f"""
<!DOCTYPE html>
<html>
<head>
    <title>CGI Sum</title>
</head>
<body>
    <h1>Sum Calculator</h1>
    <p>{a} + {b} = <strong>{result}</strong></p>

    <form method="GET" action="/cgi-bin/sum.py">
        <input name="a" placeholder="First number">
        <input name="b" placeholder="Second number">
        <button type="submit">Calculate</button>
    </form>
</body>
</html>
""")

    except ValueError:
        print("Status: 400 Bad Request")
        print("Content-Type: text/html")
        print()
        print("<h1>400 Bad Request</h1>")
        print("<p>Parameters 'a' and 'b' must be numbers.</p>")


if __name__ == "__main__":
    main()