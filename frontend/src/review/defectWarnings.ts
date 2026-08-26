/**
 * 「确认本组」就是答复的那几条警告。
 *
 * 导入器和拆分器留在病害上的警告分两类。一类是**缺东西**——构件没绑、标度不在允许范围、
 * 照片编号没核对——这些必须先补上，确认按钮该锁着。另一类的诉求只有一句"请人工确认"：
 * 机器拿不准，需要有人看一眼拍板。对后者来说，「确认本组」本身就是那次拍板，再拿它去
 * 锁确认按钮就成了死循环——警告只有确认才消得掉，确认又被警告挡着。
 *
 * 所以这份名单里的 code 有两条特权，两边必须用同一份名单，否则要么按钮点不动、
 * 要么点了警告还在：
 *
 *   1. `defectPhotoReviewModel` 判断能否单条确认时不把它们算作阻断；
 *   2. `reviewDraft` 的 confirm_defect_groups 在确认时把它们从 warnings 里清掉。
 *
 * 判断标准不是"这条警告要不要紧"，而是**它会不会自己消失**。导入器和拆分器写下的警告
 * 是"当时那一刻的观察"，此后没有任何东西重算它们——人工把数据补齐了，警告照样挂着。
 * 所以只要一条 per-defect 警告不在这份名单里，它就等于把那条病害永久钉死在待确认上。
 *
 * 真正的把关交给**实时派生检查**：构件没绑有 component_required，标度不对有
 * scale_not_allowed，评定树没选有 rating_tree_node_required，照片编号没核对有
 * photo_reference_pending——这些每次渲染都重算，数据没补齐就一直挡着，补齐了自动放行。
 * 导入器警告的职责只是"提醒人看一眼"，看完了就该由确认来了结。
 *
 * 往里加 code 之前先问一句：这条警告背后真的缺了数据吗？如果缺，那份数据有没有对应的
 * 实时检查兜着？兜得住才能进。
 */
export const HUMAN_ACKNOWLEDGEABLE_DEFECT_WARNINGS: ReadonlySet<string> = new Set([
  // 范围拆分产出的每条新病害都要人工核对构件、病害与照片的对应关系。
  "component_range_split_review_required",
  // 尺寸解析是启发式的：原文里还剩没被认领的数字就会报，而剩下的往往是位置里的
  // 墩号、桩号这类与尺寸无关的数（"近20#墩1m处"）。真假只有人分得出。
  "measurement_parse_low_confidence",
  // 来源软件的结构化尺寸与描述原文对不上。数据没缺——来源值已经保留下来了，报这条
  // 只是要人确认该信哪个。没什么可"补"的，除了确认也无从了结。
  "source_measurement_conflict",
  // Word 表格里的标度不是正整数，于是没写进 defect_scale。缺口本身由实时的
  // scale_not_allowed 挡着：人不选出一个合法标度就仍然确认不了，这条只是提醒来源有问题。
  "defect_scale_invalid",
]);

export function isHumanAcknowledgeableWarning(code: string): boolean {
  return HUMAN_ACKNOWLEDGEABLE_DEFECT_WARNINGS.has(code);
}
