#!/usr/bin/python3
import cgi
import os
def main():
    form = cgi.FieldStorage() # parse query
    method = os.environ["REQUEST_METHOD"]
    print('Content-Type: text/html\n')
    print('<html>')
    print('<head>')
    print('<meta charset="utf-8">')
    print('<title>%s</title>' % method)
    print('</head>')
    print('<body>')
    print('<h2>Query data: </h2>')
    print('<ul>')
    for key in form:
        print('<li>' + key + ': ' + form[key].value + '</li>')
    print('</ul>')
    print('</body>')
    print('</html>')
main()