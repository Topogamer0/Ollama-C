#!/usr/bin/env python3
"""
A fake Ollama server for the integration tests.

Usage: mock_server.py <test-binary> [args...]

Starts the server on a free local port, runs the test binary with OLLAMA_HOST
pointing to it and exits with the binary's exit code.
"""

import hashlib
import json
import os
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LAST_REQUEST = {}

MODEL_DETAILS = {
  'parent_model': '',
  'format': 'gguf',
  'family': 'llama',
  'families': ['llama'],
  'parameter_size': '3.2B',
  'quantization_level': 'Q4_K_M',
}


def timings(**extra):
  return {
    'model': 'llama3.2',
    'created_at': '2024-01-01T00:00:00Z',
    'done': True,
    'done_reason': 'stop',
    'total_duration': 1500000000,
    'load_duration': 1000000,
    'prompt_eval_count': 10,
    'prompt_eval_duration': 2000000,
    'eval_count': 3,
    'eval_duration': 3000000,
    **extra,
  }


class Handler(BaseHTTPRequestHandler):
  protocol_version = 'HTTP/1.1'

  def log_message(self, *args):
    pass

  # ---- helpers ----

  def read_body(self):
    length = int(self.headers.get('Content-Length') or 0)
    return self.rfile.read(length) if length else b''

  def send_json(self, obj, status=200):
    data = json.dumps(obj).encode()
    self.send_response(status)
    self.send_header('Content-Type', 'application/json; charset=utf-8')
    self.send_header('Content-Length', str(len(data)))
    self.end_headers()
    self.wfile.write(data)

  def send_error_json(self, message, status):
    self.send_json({'error': message}, status)

  def send_stream(self, parts, delay=0.01):
    self.send_response(200)
    self.send_header('Content-Type', 'application/x-ndjson')
    self.send_header('Transfer-Encoding', 'chunked')
    self.end_headers()
    for part in parts:
      line = (json.dumps(part) + '\n').encode()
      self.wfile.write(b'%x\r\n%s\r\n' % (len(line), line))
      self.wfile.flush()
      time.sleep(delay)
    self.wfile.write(b'0\r\n\r\n')
    self.wfile.flush()

  def record(self, body):
    LAST_REQUEST.clear()
    LAST_REQUEST.update({
      'method': self.command,
      'path': self.path,
      'headers': {k.lower(): v for k, v in self.headers.items()},
      'body': body.decode('utf-8', 'replace'),
    })

  # ---- routing ----

  def do_GET(self):
    if self.path == '/_last':
      return self.send_json(LAST_REQUEST)
    self.record(b'')
    if self.path == '/api/tags':
      return self.send_json({
        'models': [
          {
            'name': 'llama3.2:latest',
            'model': 'llama3.2:latest',
            'modified_at': '2024-01-01T00:00:00Z',
            'size': 2019393189,
            'digest': 'a80c4f17acd5',
            'details': MODEL_DETAILS,
          },
          {
            'name': 'nomic-embed-text:latest',
            'model': 'nomic-embed-text:latest',
            'modified_at': '2024-01-02T00:00:00Z',
            'size': 274302450,
            'digest': '0a109f422b47',
            'details': {**MODEL_DETAILS, 'family': 'nomic-bert', 'families': ['nomic-bert']},
          },
        ]
      })
    if self.path == '/api/ps':
      return self.send_json({
        'models': [{
          'name': 'llama3.2:latest',
          'model': 'llama3.2:latest',
          'size': 3825819519,
          'digest': 'a80c4f17acd5',
          'details': MODEL_DETAILS,
          'expires_at': '2024-01-01T00:05:00Z',
          'size_vram': 3825819519,
          'context_length': 4096,
        }]
      })
    self.send_error_json('not found', 404)

  def do_DELETE(self):
    body = self.read_body()
    self.record(body)
    if self.path != '/api/delete':
      return self.send_error_json('not found', 404)
    if json.loads(body).get('model') == 'missing':
      return self.send_error_json("model 'missing' not found", 404)
    self.send_response(200)
    self.send_header('Content-Length', '0')
    self.end_headers()

  def do_POST(self):
    body = self.read_body()
    self.record(body)

    if self.path.startswith('/api/blobs/'):
      digest = 'sha256:' + hashlib.sha256(body).hexdigest()
      if self.path != '/api/blobs/' + digest:
        return self.send_error_json('digest mismatch', 400)
      self.send_response(201)
      self.send_header('Content-Length', '0')
      self.end_headers()
      return

    try:
      req = json.loads(body) if body else {}
    except ValueError:
      return self.send_error_json('invalid JSON', 400)

    model = req.get('model')
    if model == 'missing':
      return self.send_error_json(f"model '{model}' not found", 404)
    if model == 'plain-error':
      data = b'internal failure'
      self.send_response(500)
      self.send_header('Content-Length', str(len(data)))
      self.end_headers()
      self.wfile.write(data)
      return

    route = {
      '/api/generate': self.generate,
      '/api/chat': self.chat,
      '/api/embed': self.embed,
      '/api/embeddings': self.embeddings,
      '/api/pull': self.progress,
      '/api/push': self.progress,
      '/api/create': self.progress,
      '/api/show': self.show,
      '/api/copy': self.copy,
      '/v1/systemone': self.systemone,
    }.get(self.path)
    if not route:
      return self.send_error_json('not found', 404)
    route(req)

  # ---- endpoints ----

  def generate(self, req):
    if req.get('stream') is False:
      return self.send_json(timings(response='Hello, world!', context=[1, 2, 3]))
    parts = [{'model': 'llama3.2', 'response': word, 'done': False} for word in ['Hello', ', ', 'world', '!']]
    if req.get('model') == 'midstream-error':
      return self.send_stream(parts[:2] + [{'error': 'something went wrong'}])
    self.send_stream(parts + [timings(response='', context=[1, 2, 3])])

  def chat(self, req):
    messages = req.get('messages', [])
    if req.get('tools'):
      message = {
        'role': 'assistant',
        'content': '',
        'tool_calls': [{'function': {'name': 'get_weather', 'arguments': {'city': 'Rome', 'days': 2}}}],
      }
    else:
      message = {'role': 'assistant', 'content': f'You said: {messages[-1]["content"] if messages else ""}'}

    if req.get('stream') is False:
      return self.send_json(timings(message=message))

    parts = []
    if req.get('think'):
      parts += [{'model': 'llama3.2', 'message': {'role': 'assistant', 'content': '', 'thinking': t}, 'done': False}
                for t in ['Let me ', 'think.']]
    if message.get('tool_calls'):
      parts.append({'model': 'llama3.2', 'message': message, 'done': False})
    else:
      text = message['content']
      parts += [{'model': 'llama3.2', 'message': {'role': 'assistant', 'content': text[i:i + 4]}, 'done': False}
                for i in range(0, len(text), 4)]
    parts.append(timings(message={'role': 'assistant', 'content': ''}))
    self.send_stream(parts)

  def embed(self, req):
    inputs = req.get('input')
    if isinstance(inputs, str):
      inputs = [inputs]
    self.send_json(timings(embeddings=[[0.1 * (i + 1), 0.2, -0.3] for i in range(len(inputs))]))

  def embeddings(self, req):
    self.send_json({'embedding': [0.5, -0.25, 1e-3]})

  def progress(self, req):
    steps = [
      {'status': 'pulling manifest'},
      {'status': 'downloading', 'digest': 'sha256:abc', 'total': 100, 'completed': 50},
      {'status': 'downloading', 'digest': 'sha256:abc', 'total': 100, 'completed': 100},
      {'status': 'success'},
    ]
    if req.get('stream') is False:
      return self.send_json(steps[-1])
    self.send_stream(steps)

  def show(self, req):
    self.send_json({
      'modelfile': '# Modelfile\nFROM llama3.2',
      'parameters': 'stop "<|eot_id|>"',
      'template': '{{ .Prompt }}',
      'license': 'LLAMA 3.2 COMMUNITY LICENSE',
      'details': MODEL_DETAILS,
      'model_info': {'general.architecture': 'llama', 'llama.context_length': 131072},
      'capabilities': ['completion', 'tools'],
      'modified_at': '2024-01-01T00:00:00Z',
    })

  def copy(self, req):
    if req.get('source') == 'missing':
      return self.send_error_json("model 'missing' not found", 404)
    self.send_response(200)
    self.send_header('Content-Length', '0')
    self.end_headers()

  def systemone(self, req):
    self.send_json({
      'model': req['model'],
      'answers': {
        'is_question': {'type': 'noul', 'noul': 0.9},
        'topic': {'type': 'choice', 'choice': 'science', 'probabilities': {'science': 0.8, 'art': 0.2},
                  'confidence': 0.8},
      },
      'usage': {'input_tokens': 42, 'output_tokens': 2},
    })


class Server(ThreadingHTTPServer):
  daemon_threads = True

  def handle_error(self, request, client_address):
    # the tests close some streams before their end
    if not isinstance(sys.exc_info()[1], (BrokenPipeError, ConnectionResetError)):
      super().handle_error(request, client_address)


def main():
  if len(sys.argv) < 2:
    print(__doc__)
    return 2
  server = Server(('127.0.0.1', 0), Handler)
  threading.Thread(target=server.serve_forever, daemon=True).start()
  env = dict(os.environ, OLLAMA_HOST=f'127.0.0.1:{server.server_address[1]}')
  env.pop('OLLAMA_API_KEY', None)
  try:
    return subprocess.run(sys.argv[1:], env=env).returncode
  finally:
    server.shutdown()


if __name__ == '__main__':
  sys.exit(main())
