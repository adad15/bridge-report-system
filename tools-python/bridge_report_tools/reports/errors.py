from __future__ import annotations

from dataclasses import dataclass


@dataclass
class ReportTemplateError(Exception):
    """模板无法被读取或解析到能出校验结论的程度。

    与"校验不通过"不同：校验不通过会返回一份带明细的 TemplateValidationResult，
    这个异常只用于连明细都出不来的情况（不是 zip、缺 document.xml、被判定为不安全包）。
    """

    code: str
    message: str

    def __str__(self) -> str:
        return f"{self.code}: {self.message}"


@dataclass
class ReportBuildError(Exception):
    """装配报告时无法继续。

    一律中止，不产出"少了一块"的文件：报告是交付物，缺内容比不出报告危险得多。
    code 稳定，供任务记录状态和测试断言。
    """

    code: str
    message: str

    def __str__(self) -> str:
        return f"{self.code}: {self.message}"
