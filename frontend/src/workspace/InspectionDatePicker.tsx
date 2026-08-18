import { forwardRef, useEffect, useMemo, useRef, useState } from "react";

/* 检查日期选择器。原生 <input type="date"> 只能一格一格改数字，翻到几年前要按很久；
   这里补上"日 → 月 → 年"三级钻取：点标题往上一级，点格子往下一级。
   输入框仍是可手输的文本框——报告上的日期常常是照着纸质件敲进来的，比点日历快。 */

const kDayNames = ["一", "二", "三", "四", "五", "六", "日"];
const kMonthNames = [
  "一月", "二月", "三月", "四月", "五月", "六月",
  "七月", "八月", "九月", "十月", "十一月", "十二月",
];
// 年份面板一屏 15 格（3 列 × 5 行），以当前年为中心向两侧各展开 7 年。
const kYearsPerPage = 15;

type View = "day" | "month" | "year";

const pad = (value: number) => String(value).padStart(2, "0");

export function formatDate(year: number, month: number, day: number): string {
  return `${year}-${pad(month + 1)}-${pad(day)}`;
}

/* 换成文本框之后浏览器不再管格式，必须自己挡住半截日期和不存在的日期。
   用 Date 反查是为了排除 2026-02-30 这种"格式对但日子不存在"的输入。 */
export function isCompleteDate(value: string): boolean {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(value)) return false;
  const [year, month, day] = value.split("-").map(Number);
  if (month < 1 || month > 12 || day < 1) return false;
  const probe = new Date(year, month - 1, day);
  return probe.getFullYear() === year && probe.getMonth() === month - 1 && probe.getDate() === day;
}

function parseDate(value: string): { year: number; month: number; day: number } | null {
  if (!isCompleteDate(value)) return null;
  const [year, month, day] = value.split("-").map(Number);
  return { year, month: month - 1, day };
}

// 周一起排，与国内表单习惯一致；JS 的 getDay() 是周日起，这里换算一下。
function mondayFirstOffset(year: number, month: number): number {
  return (new Date(year, month, 1).getDay() + 6) % 7;
}

interface DayCell {
  key: string;
  day: number;
  year: number;
  month: number;
  outside: boolean;
}

function buildDayGrid(year: number, month: number): DayCell[] {
  const cells: DayCell[] = [];
  const lead = mondayFirstOffset(year, month);
  const daysInMonth = new Date(year, month + 1, 0).getDate();
  const daysInPrev = new Date(year, month, 0).getDate();
  for (let index = lead - 1; index >= 0; index -= 1) {
    const day = daysInPrev - index;
    const date = new Date(year, month - 1, day);
    cells.push({
      key: `prev-${day}`, day,
      year: date.getFullYear(), month: date.getMonth(), outside: true,
    });
  }
  for (let day = 1; day <= daysInMonth; day += 1) {
    cells.push({ key: `cur-${day}`, day, year, month, outside: false });
  }
  // 补满 6 行，面板高度才不会随月份跳动。
  for (let day = 1; cells.length < 42; day += 1) {
    const date = new Date(year, month + 1, day);
    cells.push({
      key: `next-${day}`, day,
      year: date.getFullYear(), month: date.getMonth(), outside: true,
    });
  }
  return cells;
}

interface Props {
  id: string;
  value: string;
  onChange: (value: string) => void;
  disabled?: boolean;
  required?: boolean;
}

export const InspectionDatePicker = forwardRef<HTMLInputElement, Props>(
  function InspectionDatePicker({ id, value, onChange, disabled, required }, ref) {
    const [open, setOpen] = useState(false);
    const [view, setView] = useState<View>("day");
    const selected = useMemo(() => parseDate(value), [value]);
    const today = useMemo(() => new Date(), []);
    // 面板停在哪个年月，与已选值分开：翻页时不该动用户已经填好的日期。
    const [cursor, setCursor] = useState(() => ({
      year: selected?.year ?? today.getFullYear(),
      month: selected?.month ?? today.getMonth(),
    }));
    const containerRef = useRef<HTMLDivElement>(null);

    // 每次重新打开都回到已选日期所在的月份，并退回日视图——上次钻到年份面板
    // 停在那里，下次打开会让人不知道自己在看什么。
    useEffect(() => {
      if (!open) return;
      setView("day");
      setCursor({
        year: selected?.year ?? today.getFullYear(),
        month: selected?.month ?? today.getMonth(),
      });
    }, [open]);

    useEffect(() => {
      if (!open) return;
      const onPointerDown = (event: MouseEvent) => {
        if (!containerRef.current?.contains(event.target as Node)) setOpen(false);
      };
      const onKeyDown = (event: KeyboardEvent) => {
        if (event.key === "Escape") setOpen(false);
      };
      document.addEventListener("mousedown", onPointerDown);
      document.addEventListener("keydown", onKeyDown);
      return () => {
        document.removeEventListener("mousedown", onPointerDown);
        document.removeEventListener("keydown", onKeyDown);
      };
    }, [open]);

    const yearPageStart = cursor.year - Math.floor(kYearsPerPage / 2);

    function step(direction: -1 | 1) {
      if (view === "day") {
        const next = new Date(cursor.year, cursor.month + direction, 1);
        setCursor({ year: next.getFullYear(), month: next.getMonth() });
      } else if (view === "month") {
        setCursor((current) => ({ ...current, year: current.year + direction }));
      } else {
        setCursor((current) => ({ ...current, year: current.year + direction * kYearsPerPage }));
      }
    }

    function pickDay(cell: DayCell) {
      onChange(formatDate(cell.year, cell.month, cell.day));
      setOpen(false);
    }

    const title =
      view === "day" ? `${cursor.year}年${cursor.month + 1}月`
      : view === "month" ? `${cursor.year}年`
      : `${yearPageStart}年 - ${yearPageStart + kYearsPerPage - 1}年`;

    return (
      <div className="date-picker" ref={containerRef}>
        <div className="date-picker-control">
          <input
            id={id}
            ref={ref}
            type="text"
            inputMode="numeric"
            autoComplete="off"
            placeholder="yyyy-mm-dd"
            required={required}
            disabled={disabled}
            value={value}
            onChange={(event) => onChange(event.target.value)}
          />
          <button
            type="button"
            className="date-picker-toggle"
            aria-label="选择检查日期"
            aria-expanded={open}
            disabled={disabled}
            onClick={() => setOpen((current) => !current)}
          >
            {/* 只作装饰，读屏由上面的 aria-label 承担。 */}
            <span aria-hidden="true">▦</span>
          </button>
        </div>

        {open ? (
          <div className="date-picker-panel" role="dialog" aria-label="检查日期选择">
            <div className="date-picker-header">
              <button type="button" aria-label="上一页" onClick={() => step(-1)}>《</button>
              {/* 点标题往上一级：日 → 月 → 年。年视图已经是顶层，不再往上。 */}
              <button
                type="button"
                className="date-picker-title"
                onClick={() => setView(view === "day" ? "month" : "year")}
                disabled={view === "year"}
              >
                {title}
              </button>
              <button type="button" aria-label="下一页" onClick={() => step(1)}>》</button>
            </div>

            {view === "day" ? (
              <>
                <div className="date-picker-weekdays">
                  {kDayNames.map((name) => <span key={name}>{name}</span>)}
                </div>
                <div className="date-picker-grid date-picker-days">
                  {buildDayGrid(cursor.year, cursor.month).map((cell) => {
                    const isSelected = selected !== null && selected.year === cell.year &&
                      selected.month === cell.month && selected.day === cell.day;
                    const isToday = today.getFullYear() === cell.year &&
                      today.getMonth() === cell.month && today.getDate() === cell.day;
                    return (
                      <button
                        key={cell.key}
                        type="button"
                        className={[
                          cell.outside ? "is-outside" : "",
                          isSelected ? "is-selected" : "",
                          isToday && !isSelected ? "is-today" : "",
                        ].filter(Boolean).join(" ")}
                        aria-label={formatDate(cell.year, cell.month, cell.day)}
                        onClick={() => pickDay(cell)}
                      >
                        {cell.day}
                      </button>
                    );
                  })}
                </div>
              </>
            ) : null}

            {view === "month" ? (
              <div className="date-picker-grid date-picker-months">
                {kMonthNames.map((name, index) => (
                  <button
                    key={name}
                    type="button"
                    className={selected?.year === cursor.year && selected?.month === index
                      ? "is-selected" : ""}
                    onClick={() => {
                      setCursor((current) => ({ ...current, month: index }));
                      setView("day");
                    }}
                  >
                    {name}
                  </button>
                ))}
              </div>
            ) : null}

            {view === "year" ? (
              <div className="date-picker-grid date-picker-years">
                {Array.from({ length: kYearsPerPage }, (_, index) => yearPageStart + index)
                  .map((year) => (
                    <button
                      key={year}
                      type="button"
                      className={selected?.year === year ? "is-selected" : ""}
                      onClick={() => {
                        setCursor((current) => ({ ...current, year }));
                        setView("month");
                      }}
                    >
                      {year}年
                    </button>
                  ))}
              </div>
            ) : null}

            <div className="date-picker-footer">
              <button
                type="button"
                onClick={() => { onChange(""); setOpen(false); }}
              >
                清除
              </button>
            </div>
          </div>
        ) : null}
      </div>
    );
  }
);
