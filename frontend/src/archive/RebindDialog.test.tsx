import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { ArchiveObservation } from "../api/componentArchiveApi";
import { chooseOption } from "../test/antd";
import { RebindDialog } from "./RebindDialog";

function makeObservation(overrides: Partial<ArchiveObservation> = {}): ArchiveObservation {
  return {
    id: "obs-1",
    system_number: "BHGC-000001",
    inspection_year: 2025,
    defect_thread_id: "thread-1",
    defect_type: "蜂窝、麻面",
    defect_location: "左侧端部",
    scale: "2",
    defect_description: "左侧端部蜂窝、麻面",
    review_status: "已确认",
    updated_at: "token",
    measurements: [],
    photos: [],
    ...overrides,
  };
}

const threads = [
  {
    id: "thread-1",
    system_number: "BHXS-000001",
    thread_name: "蜂窝、麻面｜左侧端部",
    defect_type: "蜂窝、麻面",
    defect_location: "左侧端部",
    current_status: "不确定",
    confirmation_status: "人工已确认",
    first_seen_year: 2024,
    latest_seen_year: 2025,
  },
  {
    id: "thread-2",
    system_number: "BHXS-000002",
    thread_name: "横向裂缝｜底板跨中",
    defect_type: "横向裂缝",
    defect_location: "底板跨中",
    current_status: "不确定",
    confirmation_status: "人工已确认",
    first_seen_year: 2024,
    latest_seen_year: 2024,
  },
];

describe("RebindDialog", () => {
  afterEach(() => {
    vi.restoreAllMocks();
    vi.unstubAllGlobals();
  });

  it("requires the explicit confirmation checkbox before changing an existing binding", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ bound: true, observation_id: "obs-1", observation_updated_at: "next" }),
    });
    vi.stubGlobal("fetch", fetchMock);
    const onSuccess = vi.fn();
    render(
      <RebindDialog observation={makeObservation()} threads={threads} onClose={vi.fn()} onSuccess={onSuccess} />
    );

    await chooseOption(screen.getByLabelText("目标线索"), "横向裂缝｜底板跨中");
    const submit = screen.getByRole("button", { name: "确认绑定" });
    expect(submit).toBeDisabled();

    fireEvent.click(screen.getByRole("checkbox"));
    expect(submit).toBeEnabled();
    fireEvent.click(submit);

    await waitFor(() => expect(onSuccess).toHaveBeenCalled());
    const [, init] = fetchMock.mock.calls[0] as [string, RequestInit];
    expect(JSON.parse(init.body as string)).toMatchObject({
      defect_thread_id: "thread-2",
      confirm_rebind: true,
      expected_observation_updated_at: "token",
    });
  });

  it("supports explicit unbinding through the empty option", async () => {
    render(
      <RebindDialog observation={makeObservation()} threads={threads} onClose={vi.fn()} onSuccess={vi.fn()} />
    );

    await chooseOption(screen.getByLabelText("目标线索"), "（解绑，保持未绑定）");
    // 解绑同样属于改变既有绑定，需要显式确认。
    expect(screen.getByRole("button", { name: "确认绑定" })).toBeDisabled();
    expect(screen.getByRole("checkbox")).toBeInTheDocument();
  });
});
