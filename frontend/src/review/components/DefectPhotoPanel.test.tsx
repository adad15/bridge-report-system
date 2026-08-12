import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "../../api/apiClient";
import { deleteUploadedPhoto, uploadDefectPhoto } from "../../api/defectPhotoApi";
import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import { buildDefectPhotoCards } from "../defectPhotoCards";
import { data as fixtureData } from "../testFixtures";
import { DefectPhotoPanel } from "./DefectPhotoPanel";

vi.mock("../../api/defectPhotoApi", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../api/defectPhotoApi")>();
  return { ...actual, uploadDefectPhoto: vi.fn(), deleteUploadedPhoto: vi.fn() };
});

const mockedUpload = vi.mocked(uploadDefectPhoto);
const mockedDelete = vi.mocked(deleteUploadedPhoto);

interface PanelOptions {
  dispatch?: ReturnType<typeof vi.fn>;
  disabled?: boolean;
  editLockToken?: string | null;
  allowUpload?: boolean;
}

function renderPanel(draft: BridgeAnnualInspectionData, options: PanelOptions = {}) {
  const dispatch = options.dispatch ?? vi.fn();
  const defect = draft.defects[0];
  render(
    <DefectPhotoPanel
      draft={draft}
      defect={defect}
      cards={buildDefectPhotoCards(draft, defect)}
      importRecordId="record-1"
      baseUrl="http://backend"
      dispatch={dispatch}
      disabled={options.disabled ?? false}
      editLockToken={options.editLockToken ?? "lock-1"}
      allowUpload={options.allowUpload ?? true}
    />,
  );
  return dispatch;
}

function uploadedPhoto(draft: BridgeAnnualInspectionData) {
  return {
    ...draft.photos[0],
    candidate_id: "manual_photo_0001",
    photo_number: "补-1",
    linked_defect_candidate_id: "defect_0001",
    source_ref: { source_type: "manual" as const },
    extracted_file: {
      temporary_file_name: "IMG_2031.jpg",
      original_caption: "补拍",
      archive_relative_path: "photos/manual.jpg",
    },
  };
}

beforeEach(() => {
  mockedUpload.mockReset();
  mockedDelete.mockReset();
});

afterEach(() => {
  vi.restoreAllMocks();
});

describe("DefectPhotoPanel", () => {
  it("does not expose a separate photo confirmation action", () => {
    renderPanel(fixtureData());

    expect(screen.queryByRole("button", { name: "确认照片" })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "撤销确认" })).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "删除照片" })).toBeInTheDocument();
  });

  it("returns a Word photo to the unassigned list only after confirmation", () => {
    const draft = fixtureData();
    const dispatch = renderPanel(draft);
    const confirmSpy = vi.spyOn(window, "confirm").mockReturnValue(false);

    fireEvent.click(screen.getByRole("button", { name: "删除照片" }));
    expect(confirmSpy).toHaveBeenCalledWith("确定删除这张照片？它会退回未归属照片清单。");
    expect(dispatch).not.toHaveBeenCalled();

    confirmSpy.mockReturnValue(true);
    fireEvent.click(screen.getByRole("button", { name: "删除照片" }));
    expect(dispatch).toHaveBeenCalledWith({
      type: "unlink_photo_from_defect",
      photoCandidateId: "photo_0001",
    });
  });

  it("links an unassigned photo picked from the panel", () => {
    const draft = fixtureData();
    draft.photos[0] = { ...draft.photos[0], linked_defect_candidate_id: null };
    const dispatch = renderPanel(draft);

    fireEvent.click(screen.getByRole("button", { name: "添加照片" }));
    fireEvent.click(screen.getByRole("button", { name: "2.1-1" }));

    expect(dispatch).toHaveBeenCalledWith({
      type: "link_photo_to_defect",
      photoCandidateId: "photo_0001",
      defectCandidateId: "defect_0001",
    });
  });

  it("keeps 添加照片 available for uploading even with no unassigned photo left", () => {
    renderPanel(fixtureData());

    expect(screen.getByRole("button", { name: "添加照片" })).toBeEnabled();
    fireEvent.click(screen.getByRole("button", { name: "添加照片" }));
    expect(screen.getByText("本次导入没有未归属的照片。")).toBeInTheDocument();
    expect(screen.getByLabelText("从电脑上传照片")).toBeInTheDocument();
  });

  it("closes the upload entry when the review may not gain new candidates", () => {
    renderPanel(fixtureData(), { allowUpload: false });

    // 仅警告范围的重开校对不允许往导入里塞新东西，也就没有未归属照片可挂。
    expect(screen.getByRole("button", { name: "添加照片" })).toBeDisabled();
  });

  // Word 里写了编号却没抽出图：卡片仍要出现，否则这条缺失就没人认领了。
  it("acknowledges a photo number that has no photo behind it", () => {
    const draft = fixtureData();
    draft.photos = [];
    const dispatch = renderPanel(draft);

    expect(screen.getByText("无图")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "确认缺图" }));

    expect(dispatch).toHaveBeenCalledWith({
      type: "set_photo_reference_missing",
      defectCandidateId: "defect_0001",
      photoNumber: "2.1-1",
      missing: true,
    });
  });

  it("adds the uploaded photo to the draft exactly as the server stored it", async () => {
    const draft = fixtureData();
    const photo = uploadedPhoto(draft);
    mockedUpload.mockResolvedValue(photo);
    const dispatch = renderPanel(draft);

    fireEvent.click(screen.getByRole("button", { name: "添加照片" }));
    fireEvent.change(screen.getByLabelText("照片说明"), { target: { value: "补拍" } });
    fireEvent.change(screen.getByLabelText("从电脑上传照片"), {
      target: { files: [new File(["x"], "IMG_2031.jpg", { type: "image/jpeg" })] },
    });

    await waitFor(() => expect(dispatch).toHaveBeenCalledWith({ type: "add_photo", photo }));
    expect(mockedUpload).toHaveBeenCalledWith("http://backend", "record-1", "lock-1", {
      file: expect.any(File),
      defectCandidateId: "defect_0001",
      caption: "补拍",
    });
  });

  it("shows why an upload was rejected instead of failing silently", async () => {
    mockedUpload.mockRejectedValue(new ApiError("invalid_photo_file", "服务端原文"));
    const dispatch = renderPanel(fixtureData());

    fireEvent.click(screen.getByRole("button", { name: "添加照片" }));
    fireEvent.change(screen.getByLabelText("从电脑上传照片"), {
      target: { files: [new File(["x"], "evil.jpg", { type: "image/jpeg" })] },
    });

    expect(await screen.findByRole("alert")).toHaveTextContent("不是受支持的图片");
    expect(dispatch).not.toHaveBeenCalled();
  });

  it("deletes an uploaded photo through the endpoint rather than unlinking it", async () => {
    const draft = fixtureData();
    draft.photos = [uploadedPhoto(draft)];
    mockedDelete.mockResolvedValue(undefined);
    const dispatch = renderPanel(draft);
    vi.spyOn(window, "confirm").mockReturnValue(true);

    fireEvent.click(screen.getByRole("button", { name: "删除照片" }));

    await waitFor(() => expect(dispatch).toHaveBeenCalledWith({
      type: "remove_photo",
      photoCandidateId: "manual_photo_0001",
    }));
    expect(mockedDelete).toHaveBeenCalledWith("http://backend", "record-1", "lock-1", "manual_photo_0001");
  });

  it("locks every action in a read-only review", () => {
    const draft = fixtureData();
    draft.photos.push({
      ...draft.photos[0],
      candidate_id: "photo_0002",
      photo_number: "2.1-2",
      linked_defect_candidate_id: null,
    });
    renderPanel(draft, { disabled: true });

    expect(screen.getByRole("button", { name: "添加照片" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "删除照片" })).toBeDisabled();
    // 看图不算编辑：缩略图按钮在只读态照常可用。
    expect(screen.getByRole("button", { name: "查看照片 2.1-1" })).toBeEnabled();
  });
});
