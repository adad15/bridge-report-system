// 前端编号预览器：镜像后端 NumberingTemplate.cpp 的 placeholder_values + expand，
// 与 C++ 同以《构件编号规则》文档形式为交叉校验。权威生成仍在后端。

export interface GeneratedNumber {
  number: string;
  location: string;
}

type Placeholder = "span" | "pier" | "abutment" | "line" | "spanSupport" | "side" | "count";

interface PlaceValue {
  token: string;
  location: string;
}

// count 仅对 "count" 有意义（1..count）；其余维用 spanCount 派生。
function placeholderValues(placeholder: Placeholder, count: number, n: number): PlaceValue[] {
  const values: PlaceValue[] = [];
  switch (placeholder) {
    case "span":
      for (let k = 1; k <= n; k += 1) values.push({ token: String(k), location: `第${k}孔` });
      break;
    case "pier":
      for (let k = 1; k <= n - 1; k += 1) values.push({ token: String(k), location: `第${k}墩` });
      break;
    case "abutment":
      values.push({ token: "0", location: "第0台" });
      if (n > 0) values.push({ token: String(n), location: `第${n}台` });
      break;
    case "line":
      for (let k = 0; k <= n; k += 1) {
        const label = `${k}${k === 0 || k === n ? "#台" : "#墩"}`;
        values.push({ token: label, location: label });
      }
      break;
    // 一孔恒有两个支承（左右各一）。《规则》第10条支座自右至左编号，故记作 1、2。
    case "spanSupport":
      values.push({ token: "1", location: "第1号墩" });
      values.push({ token: "2", location: "第2号墩" });
      break;
    case "side":
      values.push({ token: "左", location: "左侧" });
      values.push({ token: "右", location: "右侧" });
      break;
    case "count":
      for (let k = 1; k <= count; k += 1) values.push({ token: String(k), location: "" });
      break;
  }
  return values;
}

// 只替换第一个出现（字面量，非正则），对应 C++ replace_first。
function replaceFirst(value: string, from: string, to: string): string {
  const pos = value.indexOf(from);
  return pos < 0 ? value : value.slice(0, pos) + to + value.slice(pos + from.length);
}

const TOKENS: ReadonlyArray<readonly [string, Placeholder]> = [
  ["{span}", "span"],
  ["{pier}", "pier"],
  ["{ab}", "abutment"],
  ["{line}", "line"],
  ["{sup}", "spanSupport"],
  ["{side}", "side"],
  ["{c1}", "count"],
  ["{c2}", "count"],
  ["{c3}", "count"],
];

// 先用 name 替换 {name}，再按占位符在模板中从左到右（外→内）嵌套迭代生成编号。
export function expandTemplate(
  pattern: string,
  name: string,
  counts: number[],
  spanCount: number
): GeneratedNumber[] {
  const resolved = replaceFirst(pattern, "{name}", name);
  const slots = TOKENS.map(([token, placeholder]) => ({ token, placeholder, pos: resolved.indexOf(token) }))
    .filter((slot) => slot.pos >= 0)
    .sort((a, b) => a.pos - b.pos);

  let countIndex = 0;
  let results: GeneratedNumber[] = [{ number: resolved, location: "" }];
  for (const slot of slots) {
    const count = slot.placeholder === "count" ? counts[countIndex++] ?? 0 : 0;
    const values = placeholderValues(slot.placeholder, count, spanCount);
    const next: GeneratedNumber[] = [];
    for (const acc of results)
      for (const value of values)
        next.push({
          number: replaceFirst(acc.number, slot.token, value.token),
          location: acc.location || value.location,
        });
    results = next;
  }
  return results;
}
