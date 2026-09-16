import { waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";

/**
 * Ant Design Select 的测试辅助。
 *
 * antd 的 Select 不是原生 <select>：选中项显示在输入框旁边，下拉选项要点开才渲染，
 * 而且渲染在 body 下的浮层里。几个下拉框都点开过时浮层会同时留在 DOM 里，
 * 所以按输入框 id 找到属于它自己的那一层。
 */

function dropdownOf(combobox: HTMLElement): HTMLElement | null {
  const list = document.getElementById(`${combobox.id}_list`);
  return list?.closest<HTMLElement>(".ant-select-dropdown") ?? null;
}

function optionElements(combobox: HTMLElement): HTMLElement[] {
  const dropdown = dropdownOf(combobox);
  return dropdown ? [...dropdown.querySelectorAll<HTMLElement>(".ant-select-item-option")] : [];
}

function matches(text: string, expected: string | RegExp): boolean {
  return typeof expected === "string" ? text === expected : expected.test(text);
}

/** 当前选中项显示的文字；没选时是空串。 */
export function selectedLabel(combobox: HTMLElement): string {
  const content = combobox.closest(".ant-select")?.querySelector(".ant-select-content-has-value");
  return content?.getAttribute("title") ?? content?.textContent ?? "";
}

/** 点开下拉框，返回全部选项的文字。 */
export async function optionLabels(combobox: HTMLElement): Promise<string[]> {
  await userEvent.click(combobox);
  await waitFor(() => {
    if (optionElements(combobox).length === 0) throw new Error("下拉选项还没有渲染出来");
  });
  const labels = optionElements(combobox).map((option) => option.textContent ?? "");
  await userEvent.keyboard("{Escape}");
  return labels;
}

/** 点开下拉框并点选文字匹配的那一项。 */
export async function chooseOption(combobox: HTMLElement, label: string | RegExp): Promise<void> {
  await userEvent.click(combobox);
  const option = await waitFor(() => {
    const found = optionElements(combobox).find((item) => matches(item.textContent ?? "", label));
    if (!found) throw new Error(`下拉框里没有「${String(label)}」`);
    return found;
  });
  await userEvent.click(option);
}

/**
 * 模拟一块指定宽度的屏幕。
 *
 * jsdom 里所有媒体查询默认都不匹配，antd 的 Grid.useBreakpoint 会把它当成最窄的手机屏，
 * 外壳收起侧栏、登录页收掉品牌区。要测宽屏下的布局，先调这个。
 */
export function emulateViewport(width: number): void {
  Object.defineProperty(window, "matchMedia", {
    writable: true,
    value: (query: string): MediaQueryList => {
      const min = /min-width:\s*(\d+)px/.exec(query);
      const max = /max-width:\s*(\d+)px/.exec(query);
      const matches = (!min || width >= Number(min[1])) && (!max || width <= Number(max[1]));
      return {
        matches,
        media: query,
        onchange: null,
        addListener: () => undefined,
        removeListener: () => undefined,
        addEventListener: () => undefined,
        removeEventListener: () => undefined,
        dispatchEvent: () => false,
      };
    },
  });
}
