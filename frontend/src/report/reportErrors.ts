// 报告模块的错误呈现：把后端的稳定错误码翻成用户看得懂、且知道下一步该做什么的话。

import { ApiError } from "../api/apiClient";
import type { TemplateIssue } from "../api/reportApi";

interface ValidationDetails {
  validation_result?: { issues?: TemplateIssue[] };
}

/**
 * 模板上传被拒时的明细。
 *
 * 后端在校验不通过时把整份校验结果放进错误体（设计 §21.1 的"按明细修改后重新上传"）。
 * 只给一句"模板不合契约"，管理员无从下手。
 */
export function templateValidationIssues(error: unknown): TemplateIssue[] {
  if (!(error instanceof ApiError)) return [];
  const details = error.details as ValidationDetails | undefined;
  return details?.validation_result?.issues ?? [];
}

/** 报告接口的错误文案。未知错误码原样带出后端的 message，不吞掉它。 */
export function reportErrorMessage(error: unknown): string {
  if (error instanceof ApiError) {
    switch (error.code) {
      case "report_template_invalid":
        return "模板未通过契约校验，未做任何改动。请按下面的明细修改后重新上传。";
      case "report_template_validator_unavailable":
        return "模板校验服务未返回结果，模板未做任何改动。请确认 Python 工具服务已启动。";
      case "report_template_in_use":
        return "这份模板已被年度报告配置引用，只能停用，不能删除。";
      case "report_preflight_blocked":
        return "生成前检查未通过，未创建任务。请先处理下面的阻断项。";
      case "report_job_not_downloadable":
        return "这个任务没有可下载的报告。";
      case "report_job_file_missing":
        return "报告文件已过期或已被清理，请重新生成。";
      case "db_unavailable":
        return "数据库暂不可用，请稍后重试。";
      default:
        return error.message;
    }
  }
  return error instanceof Error ? error.message : "请求失败，请稍后重试。";
}

/** 任务失败原因。错误码决定用户该去哪儿处理，不是只给一句"生成失败"。 */
export function reportJobFailureHint(code: string | null): string | null {
  switch (code) {
    case "report_preflight_blocked":
      return "数据校验没过。请回到「生成条件」逐条处理后重新生成。";
    case "report_template_missing":
    case "report_template_file_missing":
      return "模板不可用。请到系统管理的报告模板里重新上传或更换模板。";
    case "report_context_unavailable":
      return "组装报告上下文失败。多半是年度数据在生成期间被改动，重新生成即可。";
    case "report_tools_unavailable":
      return "Python 工具服务没有响应。请确认它已启动后重新生成。";
    case "report_field_update_failed":
      return "Word 或 WPS 没能算出目录和页码。请确认本机装有其中之一且没有卡住的实例。";
    case "report_output_invalid":
      return "成品没通过最终校验，已作废。这通常说明模板本身有问题，请检查模板后重试。";
    case "report_job_interrupted":
      return "服务重启时这个任务还没跑完，已作废。重新生成即可。";
    default:
      return null;
  }
}
