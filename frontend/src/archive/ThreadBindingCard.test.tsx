import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { UnboundObservation } from "../api/componentArchiveApi";
import { ThreadBindingCard } from "./ThreadBindingCard";

function makeObservation(overrides: Partial<UnboundObservation> = {}): UnboundObservation {
  return {
    id: "obs-1",
    system_number: "BHGC-000001",
    inspection_year: 2025,
    defect_thread_id: null,
    defect_type: "蜂窝、麻面",
    defect_location: "左侧端部",
    scale: "2",
    defect_description: "左侧端部蜂窝、麻面",
    review_status: "已确认",
    updated_at: "2026-07-14 10:00:00+08",
    measurements: [],
    photos: [],
    component: {
      id: "component-1",
      system_number: "GJ-000001",
      structure_part: "上部结构",
      component_type: "2-1#板",
      business_component_code: "上部承重构件",
    },
    ...overrides,
  };
}

const suggestion = {
  id: "thread-1",
  system_number: "BHXS-000001",
  thread_name: "蜂窝、麻面｜左侧端部",
  defect_type: "蜂窝、麻面",
  defect_location: "左侧端部",
  current_status: "不确定",
  confirmation_status: "人工已确认",
  first_seen_year: 2024,
  latest_seen_year: 2024,
  match_basis: { same_component: true, same_defect_type: true, location_exact: true, location_contains: false },
  suggestion_score: 3,
};

function stubFetchQueue(responses: Array<{ status?: number; body: unknown }>): ReturnType<typeof vi.fn> {
  const fetchMock = vi.fn();
  for (const response of responses) {
    fetchMock.mockResolvedValueOnce({
      ok: (response.status ?? 200) < 400,
      status: response.status ?? 200,
      json: async () => response.body,
    });
  }
  vi.stubGlobal("fetch", fetchMock);
  return fetchMock;
}

describe("ThreadBindingCard", () => {
  afterEach(() => {
    vi.restoreAllMocks();
    vi.unstubAllGlobals();
  });

  it("renders suggestions with match-basis badges and binds on demand", async () => {
    const fetchMock = stubFetchQueue([
      { body: { suggestions: [suggestion] } },
      { body: { bound: true, observation_id: "obs-1", observation_updated_at: "next" } },
    ]);
    const onResolved = vi.fn();
    render(<ThreadBindingCard observation={makeObservation()} onResolved={onResolved} onDismiss={vi.fn()} />);

    expect(await screen.findByText("位置全等")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "绑定该线索" }));

    await waitFor(() => expect(onResolved).toHaveBeenCalled());
    const [url, init] = fetchMock.mock.calls[1] as [string, RequestInit];
    expect(url).toContain("/api/defect-observations/obs-1/defect-thread");
    expect(JSON.parse(init.body as string)).toMatchObject({
      defect_thread_id: "thread-1",
      confirm_rebind: false,
    });
  });

  it("surfaces the mapped message for a stale concurrency token", async () => {
    stubFetchQueue([
      { body: { suggestions: [suggestion] } },
      { status: 409, body: { code: "observation_revision_conflict", message: "raw" } },
    ]);
    render(<ThreadBindingCard observation={makeObservation()} onResolved={vi.fn()} onDismiss={vi.fn()} />);

    fireEvent.click(await screen.findByRole("button", { name: "绑定该线索" }));

    expect(await screen.findByText("该观测已被其他操作更新，请刷新页面后重试。")).toBeInTheDocument();
  });

  it("creates a thread with prefilled standard type and location after validation", async () => {
    const fetchMock = stubFetchQueue([
      { body: { suggestions: [] } },
      { body: { created: true, observation_id: "obs-1", observation_updated_at: "next" } },
    ]);
    const onResolved = vi.fn();
    render(<ThreadBindingCard observation={makeObservation()} onResolved={onResolved} onDismiss={vi.fn()} />);

    fireEvent.click(await screen.findByRole("button", { name: "创建新线索" }));
    const locationInput = screen.getByLabelText("标准详细位置 obs-1");
    expect((locationInput as HTMLInputElement).value).toBe("左侧端部");

    // 位置清空后必填校验拦截，不发请求。
    fireEvent.change(locationInput, { target: { value: "  " } });
    fireEvent.click(screen.getByRole("button", { name: "确认创建并绑定" }));
    expect(await screen.findByText("标准病害类型与标准详细位置为必填项。")).toBeInTheDocument();
    expect(fetchMock).toHaveBeenCalledTimes(1);

    fireEvent.change(locationInput, { target: { value: "左侧端部靠近0#台" } });
    fireEvent.click(screen.getByRole("button", { name: "确认创建并绑定" }));
    await waitFor(() => expect(onResolved).toHaveBeenCalled());
    const [url, init] = fetchMock.mock.calls[1] as [string, RequestInit];
    expect(url).toContain("/api/defect-threads");
    expect(JSON.parse(init.body as string)).toMatchObject({
      bridge_component_id: "component-1",
      defect_type: "蜂窝、麻面",
      defect_location: "左侧端部靠近0#台",
      first_observation_id: "obs-1",
    });
  });

  it("暂不确定 collapses locally without any server call", async () => {
    const fetchMock = stubFetchQueue([{ body: { suggestions: [] } }]);
    const onDismiss = vi.fn();
    render(<ThreadBindingCard observation={makeObservation()} onResolved={vi.fn()} onDismiss={onDismiss} />);

    fireEvent.click(await screen.findByRole("button", { name: "暂不确定" }));

    expect(onDismiss).toHaveBeenCalled();
    expect(fetchMock).toHaveBeenCalledTimes(1); // 只有建议查询
  });
});
