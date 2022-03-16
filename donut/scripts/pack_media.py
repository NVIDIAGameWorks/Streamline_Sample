import sqlite3
import os
import lz4.block
import argparse
import sys

parser = argparse.ArgumentParser(description = "SQLite archive maintenance tool")
parser.add_argument('--manifest', default='package_manifest.txt', help="Package manifest file")
parser.add_argument('--database', default='media.db', help="Database file")
parser.add_argument('--dry-run', '-n', action='store_true')
parser.add_argument('--define', '-D', action='append')
parser.add_argument('--password', help="Password to encrypt the files")

args = parser.parse_args()

dry_run = args.dry_run
use_compression = True
database_file = args.database
if args.define is not None:
    defines = { s.split('=')[0] : s.split('=')[1] for s in args.define }
else:
    defines = { }

if args.password is not None:
    from Crypto.Cipher import AES
    from Crypto.Protocol import KDF

class Database:
    def __init__(self, database_file):
        self.conn = sqlite3.connect(database_file)
        self.cursor = self.conn.cursor()
        try:
            self.cursor.execute('''CREATE TABLE files (
                name TEXT PRIMARY KEY ON CONFLICT REPLACE, 
                mtime INTEGER, 
                compressed INTEGER, 
                original_size INTEGER,
                data BLOB)''')
        except:
            pass

    def getmtime(self, path):
        for row in self.cursor.execute('SELECT mtime FROM files WHERE name=? LIMIT 1', [path]):
            return row[0]
        return 0

    def store(self, path, mtime, compressed, original_size, contents):
        self.cursor.execute('INSERT INTO files (name, mtime, compressed, original_size, data) VALUES (?, ?, ?, ?, ?)', 
            [path, int(mtime), compressed, original_size, bytearray(contents)])
        self.conn.commit()

    def close(self):
        self.conn.close()
        self.conn = None
        self.cursor = None


if not dry_run:
    database = Database(database_file)

def is_intermediate_shader(path):
    # detect files that are called like "some_shader_abcd0123.bin"
    # those are intermediate build products and are not used by the app
    if path.endswith('.bin'):
        try:
            int(path[-12:-4], 16)
            return True
        except ValueError:
            return False
    else:
        return False

path_mappings = []

def normalize_path(path):
    for original, renamed in path_mappings:
        path = path.replace(original, renamed)

    path = path.replace('\\', '/')

    return path

def process_file(path):
    if is_intermediate_shader(path):
        return

    mtime = os.path.getmtime(path)
    dbpath = normalize_path(path)

    if not dry_run:
        # skip files that are already in the database if the file on disk is not newer
        if database.getmtime(dbpath) >= int(mtime):
            return

    with open(path, 'rb') as file:
        contents = file.read()

    original_size = len(contents)
    compressed = 0

    # compress certain file types with zlib
    if use_compression and (path.endswith('.chk') or path.endswith('.dds') or path.endswith('.json') or path.endswith('.bin')):
        contents = lz4.block.compress(contents, mode='high_compression', store_size=False)
        compressed = len(contents)

    print (len(contents), dbpath)

    if args.password is not None:
        contents += bytes((32 - len(contents) % 32) * '\0', 'ascii')
        
        key = KDF.PBKDF2(bytearray(args.password, 'ascii'), bytearray(dbpath, 'ascii'), dkLen = 16, count = 1000)
        iv = os.urandom(AES.block_size)
        cipher = AES.new(key, AES.MODE_CBC, iv)

        contents = iv + cipher.encrypt(contents)
        
    if not dry_run:
        # store or update the file in the database
        database.store(dbpath, mtime, compressed, original_size, contents)


with open(args.manifest, 'r') as listfile:
    filelist = listfile.readlines()

for line in filelist:
    # remove comments starting with a # sign
    hash_pos = line.find('#')
    if hash_pos >= 0:
        line = line[:hash_pos]

    # remove whitespace
    line = line.strip()

    # ignore empty and comment-only lines
    if not line:
        continue

    # apply command-line defines
    for name, value in defines.items():
        line = line.replace(name, value)

    # parse the path mappings
    if '->' in line:
        parts = line.split('->')
        path_mappings.append((parts[0].strip(), parts[1].strip()))
        continue

    if os.path.isdir(line):
        # if the line references a directory, recursively collect evertyhing from that directory
        for dirpath, dirnames, filenames in os.walk(line):
            for filename in filenames:
                path = os.path.join(dirpath, filename)
                process_file(path)
    else:
        # just take one file
        process_file(line)

if not dry_run:
    database.close()