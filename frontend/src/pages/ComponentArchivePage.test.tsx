import { render, screen } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type {
  ComponentDefectArchive,
  ComponentSummary,
} from "../api/componentArchiveApi";
import {
  fetchComponentArchive,
  fetchComponents,
  fetchThreadSuggestions,
} from "../api/componentArchiveApi";
import { ComponentArchivePage } from "./ComponentArchivePage";

vi.mock("../api/componentArchiveApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/componentArchiveApi")>();
  return {
    ...original,
    fetchComponents: vi.fn(),
    fetchComponentArchive: vi.fn(),
    fetchComponentRevisions: vi.fn(),
    fetchThreadSuggestions: vi.fn(),
    fetchUnboundObservations: vi.fn(),
  };
});

const mockedComponents = vi.mocked(fetchComponents);
const mockedArchive = vi.mocked(fetchComponentArchive);
const mockedSuggestions = vi.mocked(fetchThreadSuggestions);

function components(): ComponentSummary[] {
  return [{
    id: "c-1",
    system_number: "GJ-000001",
    structure_part: "上部结构",
    component_type: "铰缝",
    business_component_code: "1#铰缝",
    thread_count: 1,
    unbound_count: 2,
    first_seen_year: 2024,
    latest_seen_year: 2026,
    latest_score: 82.5,
    latest_score_year: 2026,
  }];
}

function archive(): ComponentDefectArchive {
  return {
    component: {
      id: "c-1",
      bridge_id: "bridge-1",
      system_number: "GJ-000001",
      structure_part: "上部结构",
      component_type: "铰缝",
      business_component_code: "1#铰缝",
      current_status: "在役",
    },
    ratings: [],
    threads: [],
    unbound_observations: [{
      id: "o-1",
      system_number: "BH-000001",
      inspection_year: 2026,
      defect_thread_id: null,
      defect_type: "渗水泛碱",
      defect_location: null,
      scale: "2",
      defect_description: "铰缝渗水泛碱",
      review_status: "已确认",
      updated_at: "2026-08-26 10:00:00+08",
      measurements: [],
      photos: [],
    }],
  };
}

function renderPage(path = "/bridges/bridge-1/components/c-1") {
  render(
    <MemoryRouter initialEntries={[path]}>
      <Routes>
        <Route path="/bridges/:bridgeId/components" element={<ComponentArchivePage />} />
        <Route
          path="/bridges/:bridgeId/components/:componentId"
          element={<ComponentArchivePage />}
        />
      </Routes>
    </MemoryRouter>,
  );
}

beforeEach(() => {
  mockedComponents.mockReset();
  mockedArchive.mockReset();
  mockedSuggestions.mockReset();
  mockedComponents.mockResolvedValue(components());
  mockedArchive.mockResolvedValue(archive());
});

describe("ComponentArchivePage", () => {
  // 模块 06 曾同时存在两套整理流程。旧整理页下线后，档案页的每个入口都必须指向工作台，
  // 否则用户会从档案页掉回一个已经不存在的地址。
  it("sends every triage entry point to the workbench", async () => {
    renderPage();

    const links = await screen.findAllByRole("link", { name: /线索整理/ });
    expect(links.length).toBeGreaterThan(0);
    for (const link of links) {
      expect(link).toHaveAttribute("href", "/bridges/bridge-1/defect-threads/triage");
    }
  });

  // 档案页是只读结果页：它报告"还有多少条没归入线索"，处理放在工作台。
  it("reports how many observations are still unbound without triaging them", async () => {
    renderPage();

    expect(await screen.findByText(/有 2 条病害观测尚未归入跨年线索/)).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "绑定" })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "创建新线索" })).not.toBeInTheDocument();
  });

  // 逐卡候选请求是旧页面卡死的直接原因，档案页从来不该发它。
  it("never asks for per-observation thread candidates", async () => {
    renderPage();

    await screen.findByRole("heading", { name: "铰缝", level: 2 });
    expect(mockedSuggestions).not.toHaveBeenCalled();
  });
});
