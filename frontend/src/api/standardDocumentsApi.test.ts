import { afterEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "./apiClient";
import {
  addStandardVersion,
  createStandardDocument,
  deleteStandardDocument,
  deleteStandardVersion,
  fetchStandardDocuments,
  replaceStandardVersionFile,
  standardDocumentsError,
  standardVersionContentUrl,
  updateStandardDocument,
  updateStandardVersion,
  type StandardVersion,
  type StandardVersionInput,
} from "./standardDocumentsApi";

const versionInput: StandardVersionInput = {
  label: "2011 版",
  code: "JTG/T H21—2011",
  kind: "official",
  nature: null,
  issuer: "中华人民共和国交通运输部",
  editor: null,
  publish_date: "2011-07-19",
  effective_date: "2011-09-01",
  withdrawn_date: null,
  announcement: null,
  source: null,
  note: null,
};

function respondWith(body: unknown) {
  const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => body });
  vi.stubGlobal("fetch", fetchMock);
  return fetchMock;
}

describe("standardDocumentsApi", () => {
  afterEach(() => {
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
  });

  it("lists every standard with its versions in one request", async () => {
    const fetchMock = respondWith({ documents: [{ id: "h21" }] });
    await expect(fetchStandardDocuments("http://backend")).resolves.toEqual([{ id: "h21" }]);
    expect(fetchMock).toHaveBeenCalledWith("http://backend/api/standard-documents");
  });

  it("creates a standard with its first version as multipart metadata plus the PDF", async () => {
    const fetchMock = respondWith({ document: { id: "h21" } });
    const file = new File(["%PDF-1.4"], "h21.pdf", { type: "application/pdf" });
    const document = { name: "公路桥梁技术状况评定标准", english_name: null, category: "技术状况评定" };

    await expect(createStandardDocument("http://backend", document, versionInput, file)).resolves.toEqual({ id: "h21" });

    const [url, init] = fetchMock.mock.calls[0];
    expect(url).toBe("http://backend/api/standard-documents");
    expect(init.method).toBe("POST");
    const form = init.body as FormData;
    expect(JSON.parse(form.get("metadata") as string)).toEqual({ document, version: versionInput });
    expect((form.get("file") as File).name).toBe("h21.pdf");
  });

  it("edits, replaces and deletes with the matching methods and encoded ids", async () => {
    const fetchMock = respondWith({ document: { id: "h21" }, deleted: true });
    const file = new File(["%PDF-1.4"], "new.pdf", { type: "application/pdf" });

    await updateStandardDocument("http://backend", "a/b", { name: "n", english_name: null, category: "c" });
    await deleteStandardDocument("http://backend", "a/b");
    await addStandardVersion("http://backend", "a/b", versionInput, file);
    await updateStandardVersion("http://backend", "v/1", versionInput);
    await replaceStandardVersionFile("http://backend", "v/1", file);
    await deleteStandardVersion("http://backend", "v/1");

    expect(fetchMock.mock.calls.map(([url, init]) => [url, init.method])).toEqual([
      ["http://backend/api/standard-documents/a%2Fb", "PATCH"],
      ["http://backend/api/standard-documents/a%2Fb", "DELETE"],
      ["http://backend/api/standard-documents/a%2Fb/versions", "POST"],
      ["http://backend/api/standard-document-versions/v%2F1", "PATCH"],
      ["http://backend/api/standard-document-versions/v%2F1/file", "PUT"],
      ["http://backend/api/standard-document-versions/v%2F1", "DELETE"],
    ]);
    expect(fetchMock.mock.calls[0][1].headers).toEqual({ "Content-Type": "application/json" });
  });

  it("builds content URLs that change when the file is replaced", () => {
    const version = {
      content_path: "/api/standard-document-versions/v1/content",
      updated_at: "2026-10-09 10:00:00",
    } as StandardVersion;
    expect(standardVersionContentUrl("http://backend/", version)).toBe(
      "http://backend/api/standard-document-versions/v1/content?v=2026-10-09+10%3A00%3A00",
    );
    expect(standardVersionContentUrl("http://backend", version, { download: true, page: 16 })).toBe(
      "http://backend/api/standard-document-versions/v1/content?v=2026-10-09+10%3A00%3A00&download=1#page=16",
    );
  });

  it("explains failures in plain language", () => {
    expect(standardDocumentsError(new ApiError("standard_version_label_taken", "x"))).toBe("这部规范里已有同名版本。");
    expect(standardDocumentsError(new ApiError("unrecognized_error_response", "请求失败（HTTP 413）"))).toBe(
      "文件超过了上传上限。",
    );
    expect(standardDocumentsError(new ApiError("db_unavailable", "数据库暂不可用，请稍后重试。"))).toBe(
      "数据库暂不可用，请稍后重试。",
    );
    expect(standardDocumentsError(new Error("boom"))).toBe("操作失败，请稍后重试。");
  });
});
