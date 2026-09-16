import { createContext, useContext, useEffect } from "react";

/** 顶栏上显示的当前桥梁：名称、状态、编号与线路。 */
export interface HeaderBridge {
  id: string;
  name: string;
  status: string;
  systemNumber: string;
  routeName: string | null;
}

export const HeaderBridgeContext = createContext<((bridge: HeaderBridge | null) => void) | null>(null);

/**
 * 把当前桥梁交给顶栏显示。
 *
 * 桥梁数据由工作区外壳加载，顶栏在它的上一层；外壳挂载时报上来，离开桥梁工作区时撤掉，
 * 顶栏就不会残留上一座桥的名字。
 */
export function useHeaderBridge(bridge: HeaderBridge | null): void {
  const setBridge = useContext(HeaderBridgeContext);
  const id = bridge?.id;
  const name = bridge?.name;
  const status = bridge?.status;
  const systemNumber = bridge?.systemNumber;
  const routeName = bridge?.routeName;

  useEffect(() => {
    if (!setBridge) return undefined;
    setBridge(id && name !== undefined && status !== undefined && systemNumber !== undefined
      ? { id, name, status, systemNumber, routeName: routeName ?? null }
      : null);
    return () => setBridge(null);
  }, [setBridge, id, name, status, systemNumber, routeName]);
}
