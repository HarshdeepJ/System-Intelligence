const http = require('http');
const fs = require('fs');
const path = require('path');

const page = path.join(__dirname, 'index.html');
http.createServer((request, response) => {
  fs.readFile(page, (error, contents) => {
    if (error) {
      response.writeHead(500, { 'Content-Type': 'text/plain' });
      response.end('Prototype could not be loaded.');
      return;
    }
    response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    response.end(contents);
  });
}).listen(4173, '127.0.0.1');
