import { ApiError, request } from "./apiClient";

const JSON_HEADERS = { "Content-Type": "application/json" };

export type StandardFamily = "technical_condition" | "maintenance";

export interface StandardPackageSummary {
  id: string;
  family: StandardFamily;
  standard_id: string;
  standard_code: string;
  standard_name: string;
  official_edition: string;
  package_version: string;
  contract_version: number;
  algorithm_id: string;
  effective_date: string;
  content_checksum: string;
  is_enabled: boolean;
  sync_status: "正常" | "故障";
  sync_error_code: string | null;
  sync_error_message: string | null;
}

export interface StandardCatalog {
  package: StandardPackageSummary;
  bridge_types: unknown[];
  component_categories: unknown[];
  defect_catalogs: unknown[];
  maintenance_levels: unknown[];
  inspection_types: unknown[];
  periodic_inspection_requirements: unknown[];
}

export async function fetchStandardPackages(baseUrl: string): Promise<StandardPackageSummary[]> {
  const body = await request<{ packages: StandardPackageSummary[] }>(`${baseUrl}/api/standards`);
  return body.packages;
}

export async function fetchStandardCatalog(
  baseUrl: string,
  packageId: string
): Promise<StandardCatalog> {
  return request(`${baseUrl}/api/standards/${encodeURIComponent(packageId)}/catalog`);
}

export async function setStandardPackageEnabled(
  baseUrl: string,
  packageId: string,
  enabled: boolean
): Promise<StandardPackageSummary> {
  const body = await request<{ package: StandardPackageSummary }>(
    `${baseUrl}/api/standards/${encodeURIComponent(packageId)}/enabled`,
    { method: "PATCH", headers: JSON_HEADERS, body: JSON.stringify({ enabled }) }
  );
  return body.package;
}

export function standardsErrorMessage(error: unknown): string {
  if (!(error instanceof ApiError)) return "规范目录操作失败，请稍后重试。";
  const stable: Record<string, string> = {
    standard_package_not_found: "规范包不存在或已不可用。",
    standard_package_unavailable: "规范包目录暂不可用，请联系管理员。",
    standard_package_fault_blocked: "故障规范包不能启用，请先修复规则包。",
    forbidden: "当前账号无权管理规范包。",
  };
  return stable[error.code] ?? error.message;
}
