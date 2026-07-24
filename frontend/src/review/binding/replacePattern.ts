// 构件绑定的批量查找替换：把报告编号按规则变换成台账编号，用于查找绑定目标。
// 设计见 docs/superpowers/specs/2026-07-24-bulk-binding-replace-design.md §3.1。
//
// 刻意不引入正则语法：查找串里除 `*` 外一律按字面处理，用户不必了解转义。
// `*` 匹配一段连续数字（至少一位），替换串第 k 个 `*` 取第 k 段数字。

export interface CompiledPattern {
  /** 命中返回替换结果；不命中返回 null（与"替换成空串"区分开）。 */
  apply(text: string): string | null;
}

export type CompileResult =
  | { ok: true; pattern: CompiledPattern }
  | { ok: false; error: string };

const WILDCARD = "*";

function escapeRegExp(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function countWildcards(value: string): number {
  return value.split(WILDCARD).length - 1;
}

export function compilePattern(find: string, replace: string): CompileResult {
  if (find === "") {
    return { ok: false, error: "查找内容不能为空。" };
  }

  const findWildcards = countWildcards(find);
  const replaceWildcards = countWildcards(replace);
  if (replaceWildcards > findWildcards) {
    return {
      ok: false,
      // 替换里多出来的 * 无从取值，属于写错而非边界情形，直接挡住。
      error: `替换内容里的 * 比查找内容多（${replaceWildcards} > ${findWildcards}），多出的无从取值。`,
    };
  }

  // 按 * 切段，各段转义后以 (\d+) 相连；两端加锚，必须整串命中。
  const source = find.split(WILDCARD).map(escapeRegExp).join("(\\d+)");
  const matcher = new RegExp(`^${source}$`);
  const replaceSegments = replace.split(WILDCARD);

  return {
    ok: true,
    pattern: {
      apply(text: string): string | null {
        const matched = matcher.exec(text);
        if (matched === null) return null;
        // 第 k 段之后接第 k 个捕获；replaceSegments 比 * 个数多一，故最后一段无捕获可接。
        return replaceSegments.reduce(
          (acc, segment, index) => (index === 0 ? segment : acc + (matched[index] ?? "") + segment),
          ""
        );
      },
    },
  };
}
