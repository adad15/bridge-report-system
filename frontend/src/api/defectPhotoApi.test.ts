import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { ApiError, setAuthToken } from "./apiClient";
import { defectPhotoErrorMessage, deleteUploadedPhoto, uploadDefectPhoto } from "./defectPhotoApi";

const photo = {
  candidate_id: "manual_photo_0001",
  photo_number: "补-1",
  linked_defect_candidate_id: "defect_0001",
  extracted_file: {
    temporary_file_name: "IMG_2031.jpg",
    original_caption: "补拍",
    archive_relative_path: "photos/manual.jpg",
  },
  match_status: "已确认",
  source_ref: { source_type: "manual" },
  confidence: 1,
  review_status: "已确认",
  warnings: [],
};

function jsonResponse(body: unknown, status = 200): Response {
  return {
    ok: status >= 200 && status < 300,
    status,
    json: async () => body,
  } as Response;
}

describe("defectPhotoApi", () => {
  const fetchMock = vi.fn();

  beforeEach(() => {
    fetchMock.mockReset();
    vi.stubGlobal("fetch", fetchMock);
    setAuthToken(null);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it("posts the file, the target defect and the caption as one multipart form", async () => {
    fetchMock.mockResolvedValue(jsonResponse({ photo }, 201));

    const result = await uploadDefectPhoto("http://backend/", "record-1", "lock-1", {
      file: new File(["x"], "IMG_2031.jpg", { type: "image/jpeg" }),
      defectCandidateId: "defect_0001",
      caption: "补拍",
    });

    expect(result).toEqual(photo);
    const [url, init] = fetchMock.mock.calls[0];
    expect(url).toBe("http://backend/api/import-records/record-1/photos");
    expect(init.method).toBe("POST");
    expect(new Headers(init.headers).get("X-Edit-Lock-Token")).toBe("lock-1");
    const form = init.body as FormData;
    expect(form.get("defect_candidate_id")).toBe("defect_0001");
    expect(form.get("caption")).toBe("补拍");
    expect(form.get("file")).toBeInstanceOf(File);
    // Content-Type 必须留给浏览器自己带 boundary，手动设了反而解析不出来。
    expect(new Headers(init.headers).get("Content-Type")).toBeNull();
  });

  it("deletes an uploaded photo by candidate id", async () => {
    fetchMock.mockResolvedValue(jsonResponse({ deleted: true }));

    await deleteUploadedPhoto("http://backend", "record-1", "lock-1", "manual_photo_0001");

    const [url, init] = fetchMock.mock.calls[0];
    expect(url).toBe("http://backend/api/import-records/record-1/photos/manual_photo_0001");
    expect(init.method).toBe("DELETE");
    expect(new Headers(init.headers).get("X-Edit-Lock-Token")).toBe("lock-1");
  });

  it("turns backend error codes into words the reviewer can act on", () => {
    expect(defectPhotoErrorMessage(new ApiError("photo_file_too_large", "x"))).toContain("大小上限");
    expect(defectPhotoErrorMessage(new ApiError("photo_not_deletable", "x"))).toContain("Word");
    // 没登记过的 code 回落到服务端原文，而不是吞掉变成万能提示。
    expect(defectPhotoErrorMessage(new ApiError("something_new", "服务端原文"))).toBe("服务端原文");
    expect(defectPhotoErrorMessage(new Error("boom"))).toBe("照片操作失败，请稍后重试。");
  });
});
