import { describe, expect, it } from "vitest";

import { createReviewDraftReducer } from "./reviewDraft";
import { data } from "./testFixtures";

function matchedData() {
  const state = data();
  state.defects[0] = {
    ...state.defects[0],
    photo_references: [{
      photo_number: "2.1-1",
      resolution: "matched",
      photo_candidate_id: "photo_0001",
      resolved_defect_candidate_id: "defect_0001",
      review_note: null,
    }],
  };
  return state;
}

describe("reviewDraftReducer", () => {
  it("deletes a defect and releases its photos for review", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    const next = reducer(state, { type: "delete_defect", candidateId: "defect_0001" });

    expect(next.defects).toEqual([]);
    expect(next.photos[0]).toMatchObject({
      linked_defect_candidate_id: null,
    });
    expect(state.defects).toHaveLength(1);
  });

  it("re-parses a measurement range and marks the group pending", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].group_review_status = "已确认";
    const next = reducer(state, {
      type: "edit_measurement_text",
      candidateId: "defect_0001",
      text: "0.5~4.0m",
    });

    expect(next.defects[0].measurements[0]).toMatchObject({
      value_type: "range",
      minimum_value: 0.5,
      maximum_value: 4,
      unit: "m",
    });
    expect(next.defects[0].group_review_status).toBe("待确认");
  });

  it("confirms a complete defect-photo group", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].review_status = "待确认";
    const next = reducer(state, { type: "confirm_defect_groups", candidateIds: ["defect_0001"] });
    expect(next.defects[0]).toMatchObject({ group_review_status: "已确认", review_status: "已确认" });
  });

  it("clears only the temporary range-split warning when confirming the group", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].warnings = [
      { code: "component_range_split_review_required", message: "待核对", severity: "warning", target_candidate_id: "defect_0001" },
      { code: "another_warning", message: "保留", severity: "warning", target_candidate_id: "defect_0001" },
    ];
    const next = reducer(state, {
      type: "confirm_defect_groups",
      candidateIds: ["defect_0001"],
    });
    expect(next.defects[0].warnings.map((warning) => warning.code)).toEqual(["another_warning"]);
    // 5.0：区间溯源不再存在于草稿，确认时由后端从关系表合成。
  });

  it("returns the same state when deleting an unknown defect", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    expect(reducer(state, { type: "delete_defect", candidateId: "missing" })).toBe(state);
  });

  it("batch confirms only the requested groups and atomically accepts unique photo matches", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].review_status = "已修改";
    state.defects[0].photo_references[0] = {
      photo_number: "2.1-1",
      resolution: "pending",
      photo_candidate_id: null,
      resolved_defect_candidate_id: null,
      review_note: null,
    };
    state.photos[0] = {
      ...state.photos[0],
      photo_number: "2.1-1",
      linked_defect_candidate_id: "defect_0001",
      extracted_file: {
        ...state.photos[0].extracted_file,
        archive_relative_path: "photos/2.1-1.jpg",
      },
    };

    const next = reducer(state, {
      type: "confirm_defect_groups",
      candidateIds: ["defect_0001"],
    });

    expect(next.defects[0]).toMatchObject({
      group_review_status: "已确认",
      review_status: "已修改",
    });
    expect(next.defects[0].photo_references[0]).toMatchObject({
      resolution: "matched",
      photo_candidate_id: "photo_0001",
      resolved_defect_candidate_id: "defect_0001",
    });
    expect(next.photos[0]).toEqual(state.photos[0]);
  });

  describe("照片归属与缺图操作", () => {
    function unlinkedPhoto() {
      const state = matchedData();
      state.photos[0] = {
        ...state.photos[0],
        linked_defect_candidate_id: null,
      };
      state.defects[0] = { ...state.defects[0], photo_references: [] };
      return state;
    }

    it("links a photo without creating a separate confirmation state", () => {
      const reducer = createReviewDraftReducer();
      const state = unlinkedPhoto();

      const next = reducer(state, {
        type: "link_photo_to_defect",
        photoCandidateId: "photo_0001",
        defectCandidateId: "defect_0001",
      });

      expect(next.photos[0]).toMatchObject({
        linked_defect_candidate_id: "defect_0001",
      });
      expect(next.defects[0].group_review_status).toBe("待确认");
      expect(state.photos[0].linked_defect_candidate_id).toBeNull();
    });

    it("marks the matching Word reference when the numbers line up", () => {
      const reducer = createReviewDraftReducer();
      const state = unlinkedPhoto();
      state.defects[0] = {
        ...state.defects[0],
        photo_references: [{
          photo_number: "2.1-1",
          resolution: "pending",
          photo_candidate_id: null,
          resolved_defect_candidate_id: null,
          review_note: null,
        }],
      };

      const next = reducer(state, {
        type: "link_photo_to_defect",
        photoCandidateId: "photo_0001",
        defectCandidateId: "defect_0001",
      });

      expect(next.defects[0].photo_references[0]).toMatchObject({
        resolution: "matched",
        photo_candidate_id: "photo_0001",
        resolved_defect_candidate_id: "defect_0001",
      });
    });

    it("returns the same state when the photo or defect is unknown", () => {
      const reducer = createReviewDraftReducer();
      const state = unlinkedPhoto();

      expect(reducer(state, {
        type: "link_photo_to_defect",
        photoCandidateId: "missing",
        defectCandidateId: "defect_0001",
      })).toBe(state);
      expect(reducer(state, {
        type: "link_photo_to_defect",
        photoCandidateId: "photo_0001",
        defectCandidateId: "missing",
      })).toBe(state);
    });

    it("unlinks a Word photo back to the unassigned list and reopens its reference", () => {
      const reducer = createReviewDraftReducer();
      const state = matchedData();

      const next = reducer(state, {
        type: "unlink_photo_from_defect",
        photoCandidateId: "photo_0001",
      });

      expect(next.photos[0]).toMatchObject({
        linked_defect_candidate_id: null,
      });
      expect(next.defects[0].photo_references[0]).toMatchObject({
        resolution: "pending",
        photo_candidate_id: null,
        resolved_defect_candidate_id: null,
      });
      expect(next.defects[0].group_review_status).toBe("待确认");
    });

    it("acknowledges a missing photo and takes it back", () => {
      const reducer = createReviewDraftReducer();
      const state = matchedData();
      state.defects[0] = {
        ...state.defects[0],
        photo_references: [{
          photo_number: "2.1-9",
          resolution: "pending",
          photo_candidate_id: null,
          resolved_defect_candidate_id: null,
          review_note: null,
        }],
      };

      const missing = reducer(state, {
        type: "set_photo_reference_missing",
        defectCandidateId: "defect_0001",
        photoNumber: "2.1-9",
        missing: true,
      });
      expect(missing.defects[0].photo_references[0].resolution).toBe("missing");

      const undone = reducer(missing, {
        type: "set_photo_reference_missing",
        defectCandidateId: "defect_0001",
        photoNumber: "2.1-9",
        missing: false,
      });
      expect(undone.defects[0].photo_references[0].resolution).toBe("pending");
    });

    // 这张图明明在本次导入里，只是没挂上——"原报告缺图"就是假话。
    it("refuses to call a photo missing when the import actually has that number", () => {
      const reducer = createReviewDraftReducer();
      const state = unlinkedPhoto();
      state.defects[0] = {
        ...state.defects[0],
        photo_references: [{
          photo_number: "2.1-1",
          resolution: "pending",
          photo_candidate_id: null,
          resolved_defect_candidate_id: null,
          review_note: null,
        }],
      };

      expect(reducer(state, {
        type: "set_photo_reference_missing",
        defectCandidateId: "defect_0001",
        photoNumber: "2.1-1",
        missing: true,
      })).toBe(state);
    });

    // 范围拆分把"两侧栏杆"拆成左右两条时，四个照片编号会原样复制给两边。
    // 左侧摘掉右侧那两张图之后，引用还在，缺图卡就一直冒出来——只能靠这个 action 了结。
    describe("移除不属于本病害的引用", () => {
      function splitCopies() {
        const state = matchedData();
        state.defects[0] = {
          ...state.defects[0],
          photo_references: [
            {
              photo_number: "2.1-1",
              resolution: "matched",
              photo_candidate_id: "photo_0001",
              resolved_defect_candidate_id: "defect_0001",
              review_note: null,
            },
            {
              photo_number: "2.1-2",
              resolution: "pending",
              photo_candidate_id: null,
              resolved_defect_candidate_id: null,
              review_note: null,
            },
          ],
        };
        state.defects.push({
          ...state.defects[0],
          candidate_id: "defect_0002",
          group_review_status: "已确认",
        });
        state.photos.push({
          ...state.photos[0],
          candidate_id: "photo_0002",
          photo_number: "2.1-2",
          linked_defect_candidate_id: "defect_0002",
        });
        return state;
      }

      it("drops the copied reference and reopens the group", () => {
        const reducer = createReviewDraftReducer();
        const state = splitCopies();

        const next = reducer(state, {
          type: "remove_photo_reference",
          defectCandidateId: "defect_0001",
          photoNumber: "2.1-2",
        });

        expect(next.defects[0].photo_references.map((item) => item.photo_number)).toEqual(["2.1-1"]);
        expect(next.defects[0].group_review_status).toBe("待确认");
        // 另一条病害留着自己的那份引用和照片，不受牵连。
        expect(next.defects[1].photo_references.map((item) => item.photo_number))
          .toEqual(["2.1-1", "2.1-2"]);
        expect(next.photos).toEqual(state.photos);
      });

      it("refuses while that number is still linked to this defect", () => {
        const reducer = createReviewDraftReducer();
        const state = splitCopies();

        // 照片还挂着时摘掉引用，卡片不会消失（照片本身照样成卡），
        // 只会让一张没人认领的图继续印进报告。
        expect(reducer(state, {
          type: "remove_photo_reference",
          defectCandidateId: "defect_0001",
          photoNumber: "2.1-1",
        })).toBe(state);
      });

      it("returns the same state for an unknown defect or number", () => {
        const reducer = createReviewDraftReducer();
        const state = splitCopies();

        expect(reducer(state, {
          type: "remove_photo_reference",
          defectCandidateId: "missing",
          photoNumber: "2.1-2",
        })).toBe(state);
        expect(reducer(state, {
          type: "remove_photo_reference",
          defectCandidateId: "defect_0001",
          photoNumber: "2.9-9",
        })).toBe(state);
      });
    });
  });

  // 确认就是这些警告要的那次"人工确认"，答复过了就不该继续挂着——留着的话下一次
  // 渲染又会把它算成待处理问题，病害永远确认不完。
  it("clears the warnings that confirmation is the answer to", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0] = {
      ...state.defects[0],
      review_status: "待确认",
      group_review_status: "待确认",
      warnings: [
        { code: "measurement_parse_low_confidence", message: "尺寸表达未能稳定结构化，请人工确认。", severity: "warning", target_candidate_id: "defect_0001" },
        { code: "component_range_split_review_required", message: "该病害由构件范围拆分，请人工核对构件、病害和照片关联。", severity: "warning", target_candidate_id: "defect_0001" },
        { code: "photo_archive_missing", message: "照片归档文件缺失。", severity: "warning", target_candidate_id: "defect_0001" },
      ],
    };

    const next = reducer(state, { type: "confirm_defect_groups", candidateIds: ["defect_0001"] });

    // 只清"人看一眼就能了结"的那两条；缺归档文件是真的缺东西，必须原样留着。
    expect(next.defects[0].warnings.map((warning) => warning.code)).toEqual(["photo_archive_missing"]);
    expect(next.defects[0].group_review_status).toBe("已确认");
  });

  it("ignores and restores a defect through explicit actions", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    const ignored = reducer(state, { type: "ignore_defect", candidateId: "defect_0001" });
    const restored = reducer(ignored, { type: "restore_ignored_defect", candidateId: "defect_0001" });

    expect(ignored.defects[0]).toMatchObject({ review_status: "已忽略", group_review_status: "待确认" });
    expect(restored.defects[0]).toMatchObject({ review_status: "待确认", group_review_status: "待确认" });
  });

  // 换绑不再是原语：先从原病害摘掉，再挂到目标病害。两侧的组都要打回待确认。
  it("relinks a photo across defects in two steps", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].group_review_status = "已确认";
    state.defects.push({
      ...state.defects[0],
      candidate_id: "defect_0002",
      group_review_status: "已确认",
      photo_references: [],
    });

    const unlinked = reducer(state, {
      type: "unlink_photo_from_defect",
      photoCandidateId: "photo_0001",
    });
    expect(unlinked.photos[0].linked_defect_candidate_id).toBeNull();
    expect(unlinked.defects[0].photo_references[0].resolution).toBe("pending");

    const relinked = reducer(unlinked, {
      type: "link_photo_to_defect",
      photoCandidateId: "photo_0001",
      defectCandidateId: "defect_0002",
    });

    expect(relinked.photos[0]).toMatchObject({
      linked_defect_candidate_id: "defect_0002",
    });
    expect(relinked.defects.map((item) => item.group_review_status)).toEqual(["待确认", "待确认"]);
  });

  it("mirrors an uploaded photo into the draft and reopens its defect group", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].group_review_status = "已确认";
    const uploaded = {
      ...state.photos[0],
      candidate_id: "manual_photo_0001",
      photo_number: "补-1",
      linked_defect_candidate_id: "defect_0001",
      source_ref: { source_type: "manual" as const },
    };

    const next = reducer(state, { type: "add_photo", photo: uploaded });

    expect(next.photos).toHaveLength(2);
    expect(next.photos[1]).toEqual(uploaded);
    expect(next.defects[0].group_review_status).toBe("待确认");
    // 上传的照片没有 Word 引用条目，原有引用一个都不动。
    expect(next.defects[0].photo_references).toEqual(state.defects[0].photo_references);
  });

  it("refuses an uploaded photo whose defect is not in this import", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    const stray = {
      ...state.photos[0],
      candidate_id: "manual_photo_0001",
      linked_defect_candidate_id: "defect_9999",
    };

    expect(reducer(state, { type: "add_photo", photo: stray })).toBe(state);
  });

  it("drops a removed upload without touching the remaining photos", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].group_review_status = "已确认";
    const uploaded = {
      ...state.photos[0],
      candidate_id: "manual_photo_0001",
      photo_number: "补-1",
      linked_defect_candidate_id: "defect_0001",
      source_ref: { source_type: "manual" as const },
    };
    const withUpload = reducer(state, { type: "add_photo", photo: uploaded });

    const next = reducer(withUpload, { type: "remove_photo", photoCandidateId: "manual_photo_0001" });

    expect(next.photos.map((item) => item.candidate_id)).toEqual(["photo_0001"]);
    expect(next.defects[0].group_review_status).toBe("待确认");
  });

  it("assigns one rating tree node to a selected source group without touching other records", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects = [
      {
        ...state.defects[0],
        candidate_id: "defect_0001",
        source_defect_group_id: "group-a",
        source_defect_indicator_id: "indicator-a",
      },
      {
        ...state.defects[0],
        candidate_id: "defect_0002",
        source_defect_group_id: "group-a",
        source_defect_indicator_id: "indicator-a",
      },
      {
        ...state.defects[0],
        candidate_id: "defect_0003",
        source_defect_group_id: "group-b",
        source_defect_indicator_id: "indicator-b",
      },
    ];

    const next = reducer(state, {
      type: "select_rating_tree_nodes",
      candidateIds: ["defect_0001", "defect_0002"],
      versionId: "tree-version-2",
      nodeId: "tree-node-other",
      nodeName: "其他病害",
      isScoring: false,
      matchEvidence: "用户按相同来源身份批量指定评定树病害",
    });

    // 5.0：选中的节点写进评分树解析表，草稿只跟着改连带变化的**来源事实**：
    // 病害名称与标度。节点 id、版本、匹配证据不再回写到这里。
    for (const defect of next.defects.slice(0, 2)) {
      expect(defect).toMatchObject({
        defect_type: "其他病害",
        defect_scale: null,
        group_review_status: "待确认",
      });
      expect("rating_tree_version_id" in defect).toBe(false);
      expect("rating_tree_node_id" in defect).toBe(false);
      expect("rating_tree_match_evidence" in defect).toBe(false);
    }
    expect(next.defects[0].source_defect_group_id).toBe("group-a");
    expect(next.defects[2]).toBe(state.defects[2]);
  });
});
