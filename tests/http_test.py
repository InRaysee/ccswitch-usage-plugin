import http.server, threading, subprocess, sys
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        if self.path == '/denied': self.send_response(403); self.end_headers(); return
        if self.path == '/redirect': self.send_response(302); self.send_header('Location','http://example.invalid/'); self.end_headers(); return
        if self.headers.get('Authorization') != 'Bearer synthetic-key\'"\\test': self.send_response(401); self.end_headers(); return
        self.send_response(200); self.end_headers(); self.wfile.write(b'{"balance":42.5}')
    def do_POST(self):
        data=self.rfile.read(int(self.headers.get('Content-Length','0')))
        if data != b'{"test":true}': self.send_response(400); self.end_headers(); return
        self.send_response(200); self.end_headers(); self.wfile.write(b'{"balance":7}')
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    result=subprocess.run([sys.argv[1],'--http',f'http://127.0.0.1:{server.server_port}'],timeout=40)
finally:
    server.shutdown(); server.server_close()
sys.exit(result.returncode)
