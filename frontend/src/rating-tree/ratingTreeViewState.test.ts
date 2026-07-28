import { beforeEach, describe, expect, it } from "vitest";

import {
  clearRatingTreeViewStateForTests,
  readRatingTreeViewState,
  writeRatingTreeViewState,
} from "./ratingTreeViewState";

describe("ratingTreeViewState", () => {
  beforeEach(clearRatingTreeViewStateForTests);

  it("keeps search, expansion and selection isolated by version", () => {
    writeRatingTreeViewState("v1", {
      searchTerm: "裂缝",
      expandedNodeIds: ["root", "beam"],
      selectedNodeId: "defect-1",
    });

    expect(readRatingTreeViewState("v1")).toEqual({
      searchTerm: "裂缝",
      expandedNodeIds: ["root", "beam"],
      selectedNodeId: "defect-1",
    });
    expect(readRatingTreeViewState("v2")).toEqual({
      searchTerm: "",
      expandedNodeIds: [],
      selectedNodeId: null,
    });
  });

  it("does not expose the stored expansion array for mutation", () => {
    const state = {
      searchTerm: "",
      expandedNodeIds: ["root"],
      selectedNodeId: null,
    };
    writeRatingTreeViewState("v1", state);
    state.expandedNodeIds.push("changed-outside");

    expect(readRatingTreeViewState("v1").expandedNodeIds).toEqual(["root"]);
  });
});
