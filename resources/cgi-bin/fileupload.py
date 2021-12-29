#!/usr/bin/python3
import cgi
import os
def main():
    form = cgi.FieldStorage() # parse query
    fileitem = form['upload']
    doc_root = os.environ['DOCUMENT_ROOT']
    upload_path = '/upload/'
    real_path = doc_root + upload_path
    hasfile = True
    if not os.path.exists(real_path):
        os.mkdir(real_path)
    if fileitem.filename:
        fn = os.path.basename(fileitem.filename.replace("\\", "/"))
        with open(real_path + fn, 'wb') as f:
            f.write(fileitem.file.read())
        message = 'The file "' + fn + '" was uploaded successfully!'
    else:
        hasfile = False
        message = 'No file was uploaded.'
    print('Content-Type: text/html\n')
    print('<html>')
    print('<head>')
    print('<meta charset="utf-8">')
    print('<title>File Upload</title>')
    print('</head>')
    print('<body>')
    print('<h2>%s</h2>' % message)
    if hasfile:
        print('<p>The uploaded file is %s</p>' % (upload_path + fn))
    print('</body>')
    print('</html>')
main()