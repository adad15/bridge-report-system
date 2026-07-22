import { useNavigate, useParams } from "react-router-dom";

import { ComponentBindingWorkspace } from "../review/binding/ComponentBindingWorkspace";
import { reviewPath } from "../workspace/workspaceState";

export function ComponentBindingPage() {
  const { bridgeId, inspectionYearId, importRecordId } = useParams<{
    bridgeId: string;
    inspectionYearId: string;
    importRecordId: string;
  }>();
  const navigate = useNavigate();

  if (!bridgeId || !inspectionYearId || !importRecordId) {
    return (
      <main className="page">
        <p className="error-text">缺少必要的路由参数。</p>
      </main>
    );
  }

  return (
    <main className="page component-binding-page">
      <ComponentBindingWorkspace
        importId={importRecordId}
        bridgeId={bridgeId}
        onEnterReview={() => navigate(reviewPath(bridgeId, inspectionYearId, importRecordId))}
      />
    </main>
  );
}
