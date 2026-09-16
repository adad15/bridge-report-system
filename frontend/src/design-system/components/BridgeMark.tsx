/** 品牌图形：斜拉桥的塔、梁和两根拉索。顶栏品牌和桥梁数量指标共用。 */
export function BridgeMark({ color, width = 42 }: { color: string; width?: number }) {
  return (
    <svg width={width} height={(width * 34) / 42} viewBox="0 0 42 34" aria-hidden="true" focusable="false">
      <g fill="none" stroke={color} strokeLinecap="round">
        <line x1="21.5" y1="3" x2="21.5" y2="29" strokeWidth="3" />
        <line x1="4" y1="27.5" x2="39" y2="27.5" strokeWidth="3" />
        <line x1="21.5" y1="9" x2="5" y2="22" strokeWidth="1.2" />
        <line x1="21.5" y1="9" x2="38" y2="22" strokeWidth="1.2" />
      </g>
    </svg>
  );
}
