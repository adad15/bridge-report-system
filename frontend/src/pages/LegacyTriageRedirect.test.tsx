import { render, screen } from "@testing-library/react";
import { MemoryRouter, Route, Routes, useLocation } from "react-router-dom";
import { describe, expect, it } from "vitest";

import { LegacyTriageRedirect } from "./LegacyTriageRedirect";

function CurrentPath() {
  return <span>{useLocation().pathname}</span>;
}

describe("LegacyTriageRedirect", () => {
  // 旧地址还留在书签和浏览器历史里；下线旧页面不该让它们 404。
  it("sends the retired review URL to the workbench under the same bridge", () => {
    render(
      <MemoryRouter initialEntries={["/bridges/bridge-1/defect-threads/review"]}>
        <Routes>
          <Route path="/bridges/:bridgeId">
            <Route path="defect-threads/review" element={<LegacyTriageRedirect />} />
            <Route path="defect-threads/triage" element={<CurrentPath />} />
          </Route>
        </Routes>
      </MemoryRouter>,
    );

    expect(screen.getByText("/bridges/bridge-1/defect-threads/triage")).toBeInTheDocument();
  });
});
