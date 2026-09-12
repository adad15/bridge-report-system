#include "bridge_report/http/Cors.hpp"

namespace bridge_report::http {

void apply_local_dev_cors_headers(const drogon::HttpResponsePtr& response) {
    // 本地 Vite 前端开发服务器。
    response->addHeader("Access-Control-Allow-Origin", "http://127.0.0.1:5173");
    response->addHeader("Access-Control-Allow-Methods", "GET, PUT, PATCH, POST, DELETE, OPTIONS");
    // If-Match 承载来源草稿并发版本（设计 §8.0）。用标准头而不是再造一个私有头，
    // 但浏览器对两者一视同仁：不在白名单里，预检就直接把请求挡在发出之前。
    response->addHeader(
        "Access-Control-Allow-Headers",
        "Content-Type, Authorization, X-Edit-Lock-Token, If-Match");
    // ETag 默认不暴露给脚本：跨域下 fetch 只能读到少数几个"安全"响应头，
    // 不显式暴露的话前端拿不到新版本号，下一次写就必然撞版本冲突。
    //
    // Content-Disposition 同理：报告下载的文件名由后端按 §20 的规则拼定，前端用
    // fetch 取 blob 时读不到这个头，存下来的就只能是一个由 URL 猜出来的名字。
    response->addHeader("Access-Control-Expose-Headers", "ETag, Content-Disposition");
}

}  // 命名空间 bridge_report::http
