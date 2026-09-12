from __future__ import annotations

from dataclasses import dataclass
from typing import Literal


Severity = Literal["error", "warning"]


@dataclass(frozen=True)
class TemplateIssue:
    """一条校验明细。

    code 稳定、可被前端和测试断言；message 面向管理员，说清楚该改模板的哪里。
    location 用扫描层的位置串（body#12、header:1#0、zip 条目名），没有具体位置时留空。
    """

    code: str
    message: str
    severity: Severity = "error"
    location: str | None = None

    def to_json(self) -> dict:
        payload: dict = {
            "code": self.code,
            "message": self.message,
            "severity": self.severity,
        }
        if self.location is not None:
            payload["location"] = self.location
        return payload


def error(code: str, message: str, location: str | None = None) -> TemplateIssue:
    return TemplateIssue(code=code, message=message, severity="error", location=location)


def warning(code: str, message: str, location: str | None = None) -> TemplateIssue:
    return TemplateIssue(code=code, message=message, severity="warning", location=location)
