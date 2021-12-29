#!/usr/bin/python3
import cgi
import os
def main():
    form = cgi.FieldStorage() # parse query
    fileitem = form['upload']
    upload_path = './resources/upload/'
    if not os.path.exists(upload_path):
        os.mkdir(upload_path)
    if fileitem.filename:
        fn = os.path.basename(fileitem.filename)
        with open(upload_path + fn, 'wb') as f:
            f.write(fileitem.file.read())
        message = 'The file "' + fn + '" was uploaded successfully!'
    else:
        message = 'No file was uploaded.'
    print('Content-Type: text/html\n')
    print('<html>')
    print('<head>')
    print('<meta charset="utf-8">')
    print('<title>File Upload</title>')
    print('</head>')
    print('<body>')
    print('<h2>%s</h2>' % message)
    print('</body>')
    print('</html>')
main()