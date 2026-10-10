import { createServer, request as forward } from "node:http";
import { readFile } from "node:fs/promises";
import { resolve, extname, sep } from "node:path";
const [directory, port, backend] = process.argv.slice(2);
const root=resolve(directory);
createServer(async (req,res)=>{
  if(req.url.startsWith("/api/") || req.url.startsWith("/health")) {
    const upstream=forward(new URL(req.url,backend),{method:req.method,headers:{...req.headers,host:new URL(backend).host}},reply=>{
      res.writeHead(reply.statusCode,reply.headers); reply.pipe(res);
    });
    upstream.on("error",()=>{ if(!res.headersSent)res.writeHead(502);res.end("Acceptance backend unavailable"); });
    req.pipe(upstream);return;
  }
  try {
    let target=resolve(root,"."+decodeURIComponent(new URL(req.url,"http://localhost").pathname));
    if(target!==root&&!target.startsWith(root+sep)){res.writeHead(403).end();return;}
    if(!extname(target))target=resolve(root,"index.html");
    const body=await readFile(target);
    res.writeHead(200,{"Content-Type":({".html":"text/html; charset=utf-8",".js":"text/javascript",".css":"text/css",".svg":"image/svg+xml"})[extname(target)]||"application/octet-stream"});
    res.end(body);
  }catch{res.writeHead(404).end();}
}).listen(Number(port),"127.0.0.1",()=>console.log("Acceptance production frontend ready"));
