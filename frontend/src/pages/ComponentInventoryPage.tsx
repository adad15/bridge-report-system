import { ComponentInventoryEditor } from "../bridges/ComponentInventoryEditor";
import { useBridgeWorkspace } from "../workspace/BridgeWorkspaceShell";

// 台账自成一页：构件可达数千条，加载一次要秒级。挂在总览页时，每次切走再切回
// 都会整份重新拉取，且不看台账的人也要为它等待。导航由工作区标签承担。
export function ComponentInventoryPage() {
  const { overview } = useBridgeWorkspace();
  return <ComponentInventoryEditor bridgeId={overview.bridge.id} />;
}
