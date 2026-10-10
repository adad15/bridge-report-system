import { Alert, Button, Skeleton } from "antd";
import { Component, Suspense, lazy, useMemo, useState, type FunctionComponent, type ReactNode } from "react";

class ResourceLoadError extends Error {
  constructor(readonly cause: unknown) {
    super("Feature resource could not be loaded");
  }
}

interface BoundaryProps {
  children: ReactNode;
  label: string;
  placeholder?: ReactNode;
  onRetry: () => void;
  onCancel?: () => void;
}

class FeatureBoundary extends Component<BoundaryProps, { error: Error | null }> {
  state: { error: Error | null } = { error: null };

  static getDerivedStateFromError(error: Error) {
    return { error };
  }

  render() {
    if (!this.state.error) return this.props.children;
    const { label, placeholder, onRetry, onCancel } = this.props;
    const resourceFailure = this.state.error instanceof ResourceLoadError;
    return (
      <section aria-label={label + "加载失败"}>
        <Alert
          type="error"
          showIcon
          title={label + (resourceFailure ? "暂时无法加载" : "显示异常")}
          description={resourceFailure
            ? "请重试。如果仍然失败，可能是连接中断或应用已更新。重新加载整个页面会丢失未保存的内容。"
            : "请重新打开此内容。其他区域仍可继续使用。"}
          action={<Button onClick={onRetry}>重试</Button>}
        />
        {placeholder}
        {onCancel ? <Button onClick={onCancel}>取消打开</Button> : null}
        {resourceFailure ? <Button onClick={() => {
          if (window.confirm("重新加载整个页面会丢失未保存的内容。确定重新加载吗？")) window.location.reload();
        }}>重新加载页面</Button> : null}
      </section>
    );
  }
}

interface LazyFeatureOptions<P> {
  label: string;
  minHeight?: number;
  placeholder?: (props: P) => ReactNode;
  cancel?: (props: P) => void;
}

/** Declare at module scope. Success is shared; retry replaces React.lazy's rejected attempt. */
export function createLazyFeature<P extends object>(
  loader: () => Promise<{ default: FunctionComponent<P> }>,
  options: LazyFeatureOptions<NoInfer<P>>,
): FunctionComponent<P> {
  let pending: ReturnType<typeof loader> | undefined;
  const load = () => {
    if (!pending) {
      const attempt = Promise.resolve().then(loader).catch((cause: unknown) => {
        if (pending === attempt) pending = undefined;
        throw new ResourceLoadError(cause);
      });
      pending = attempt;
    }
    return pending;
  };

  return function LazyFeature(props: P) {
    const [generation, setGeneration] = useState(0);
    const Loaded = useMemo(() => lazy(async () => {
      const { default: Feature } = await load();
      return { default: ({ featureProps }: { featureProps: P }) => <Feature {...featureProps} /> };
    }), [generation]);
    const placeholder = options.placeholder?.(props);
    const onCancel = options.cancel ? () => options.cancel?.(props) : undefined;
    return (
      <FeatureBoundary
        key={generation}
        label={options.label}
        placeholder={placeholder}
        onRetry={() => setGeneration((value) => value + 1)}
        onCancel={onCancel}
      >
        <Suspense fallback={
          <section role="status" aria-label={options.label + "加载中"} style={{ minHeight: options.minHeight ?? 160 }}>
            <p>正在加载{options.label}…</p>
            {placeholder ?? <Skeleton active paragraph={{ rows: 3 }} />}
            {onCancel ? <Button onClick={onCancel}>取消打开</Button> : null}
          </section>
        }>
          <Loaded featureProps={props} />
        </Suspense>
      </FeatureBoundary>
    );
  };
}
