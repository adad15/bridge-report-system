import { Button, Checkbox, Flex, Typography, theme } from "antd";

interface DefectListHeaderProps {
  /** 当前筛选结果的条数。 */
  total: number;
  selectedCount: number;
  selectableCount: number;
  allSelectableSelected: boolean;
  someSelectableSelected: boolean;
  /** 只读时不给勾选和批量确认：只剩条数。 */
  readOnly?: boolean;
  onToggleSelectAll: () => void;
  onBatchConfirm: () => void;
}

/**
 * 病害列表的表头行：全选与批量确认贴着列表本身。
 *
 * 原来这两个控件挤在顶部工具栏里，和搜索、筛选排成一长条；它们操作的是下面这张列表的
 * 勾选状态，放在列表头上才对得上。
 */
export function DefectListHeader({
  total,
  selectedCount,
  selectableCount,
  allSelectableSelected,
  someSelectableSelected,
  readOnly = false,
  onToggleSelectAll,
  onBatchConfirm,
}: DefectListHeaderProps) {
  const { token } = theme.useToken();
  return (
    <Flex
      align="center"
      justify="space-between"
      gap={8}
      wrap
      style={{ paddingBlock: 8, borderBottom: `1px solid ${token.colorSplit}` }}
    >
      {readOnly ? (
        <Typography.Text strong>病害 {total.toLocaleString()} 条</Typography.Text>
      ) : (
        <Checkbox
          checked={allSelectableSelected}
          indeterminate={someSelectableSelected}
          disabled={selectableCount === 0}
          title={`选择当前筛选结果中可批量确认的 ${selectableCount} 条病害`}
          onChange={onToggleSelectAll}
        >
          全选可确认项（{selectableCount}）
        </Checkbox>
      )}
      {readOnly ? null : (
        <Flex align="center" gap={8}>
          <Typography.Text type="secondary">共 {total.toLocaleString()} 条</Typography.Text>
          <Button size="small" type={selectedCount > 0 ? "primary" : "default"} disabled={selectedCount === 0} onClick={onBatchConfirm}>
            批量确认{selectedCount > 0 ? `（${selectedCount}）` : ""}
          </Button>
        </Flex>
      )}
    </Flex>
  );
}
