import { useEffect, useState } from "react";

import { readCached, standardCatalogsCacheKey, writeCached } from "../api/resourceCache";
import {
  fetchStandardCatalog,
  fetchStandardPackages,
  standardsErrorMessage,
  type StandardCatalog,
} from "../api/standardsApi";
import { backendBaseUrl } from "../config";

function versionParts(version: string): number[] {
  return version.split(".").map((part) => Number.parseInt(part, 10) || 0);
}

function newestPackageFirst(left: StandardCatalog, right: StandardCatalog): number {
  const a = versionParts(left.package.package_version);
  const b = versionParts(right.package.package_version);
  for (let index = 0; index < Math.max(a.length, b.length); index += 1) {
    const difference = (b[index] ?? 0) - (a[index] ?? 0);
    if (difference !== 0) return difference;
  }
  return left.package.standard_code.localeCompare(right.package.standard_code);
}

// 能用来生成台账的技术评定规范包。添加桥梁向导和"补台账"面板都要它，
// 加载逻辑只写这一份。
export function useStandardCatalogs() {
  const [catalogs, setCatalogs] = useState<StandardCatalog[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    // 规范目录是全局参考数据，与台账用同一份缓存，避免每次打开向导重拉。
    const cached = readCached<StandardCatalog[]>(standardCatalogsCacheKey);
    if (cached) {
      setCatalogs(cached);
      setLoading(false);
    } else {
      setLoading(true);
    }
    fetchStandardPackages(backendBaseUrl)
      .then(async (packages) => {
        const available = packages.filter(
          (item) => item.family === "technical_condition" && item.is_enabled && item.sync_status === "正常"
        );
        const loaded = (await Promise.all(
          available.map((item) => fetchStandardCatalog(backendBaseUrl, item.id))
        )).sort(newestPackageFirst);
        writeCached(standardCatalogsCacheKey, loaded);
        if (cancelled) return;
        setCatalogs(loaded);
        setError(null);
      })
      .catch((caught) => {
        if (!cancelled) setError(standardsErrorMessage(caught));
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, []);

  return { catalogs, loading, error };
}

// 默认使用最新可用包；用户仍可在存在多个版本时显式选择历史包。
export function useSelectedPackage(catalogs: StandardCatalog[]) {
  const [packageId, setPackageId] = useState("");
  useEffect(() => {
    setPackageId((current) =>
      current && catalogs.some((item) => item.package.id === current)
        ? current
        : (catalogs[0]?.package.id ?? "")
    );
  }, [catalogs]);
  return [packageId, setPackageId] as const;
}
