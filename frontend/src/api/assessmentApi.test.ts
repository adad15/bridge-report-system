import { beforeEach, describe, expect, it, vi } from "vitest";

import { previewAssessment } from "./assessmentApi";

describe("assessmentApi", () => {
  beforeEach(() => vi.restoreAllMocks());

  it("posts the current draft revision and edit-lock token with an abort signal", async () => {
    const controller = new AbortController();
    const response = {
      client_revision: 4,
      input_checksum: `sha256:${"a".repeat(64)}`,
      input_summary: {},
      standard: { standard_id: "JTG_H21_2011", standard_code: "JTG/T H21—2011", standard_name: "公路桥梁技术状况评定标准", official_edition: "2011", package_version: "1.0.1", content_checksum: `sha256:${"b".repeat(64)}`, algorithm_id: "jtg-h21-2011" },
      result: null,
      issues: [],
      assessment_run_id: null,
    };
    vi.spyOn(globalThis, "fetch").mockResolvedValue({ ok: true, json: async () => response } as Response);

    await expect(previewAssessment("http://backend", "record-1", { defects: [] } as never, 4, "lock-1", controller.signal)).resolves.toEqual(response);
    expect(fetch).toHaveBeenCalledWith(
      "http://backend/api/import-records/record-1/assessment-preview",
      expect.objectContaining({
        method: "POST",
        signal: controller.signal,
        headers: expect.objectContaining({ "X-Edit-Lock-Token": "lock-1" }),
        body: JSON.stringify({ draft: { defects: [] }, client_revision: 4 }),
      }),
    );
  });
});
