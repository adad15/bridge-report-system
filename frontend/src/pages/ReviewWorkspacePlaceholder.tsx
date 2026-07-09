import { useParams } from "react-router-dom";

// 占位组件：校对工作台的真正实现在 Task 13 落地，这里先把路由接通，
// 让桥梁详情页的"进入校对"链接有地方可去。
export function ReviewWorkspacePlaceholder() {
  const { importRecordId } = useParams<{ importRecordId: string }>();

  return (
    <section className="status-panel">
      <h1>校对工作台占位</h1>
      <p>校对工作台占位：{importRecordId}</p>
    </section>
  );
}
