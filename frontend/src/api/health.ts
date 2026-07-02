export type BackendHealth = {
  status: string;
  service: string;
  version: string;
  host?: string;
  port?: number;
  python_tools_base_url?: string;
  archive_root?: string;
};

export async function fetchBackendHealth(baseUrl: string): Promise<BackendHealth> {
  const response = await fetch(`${baseUrl}/health`);
  if (!response.ok) {
    throw new Error(`Backend health check failed with HTTP ${response.status}`);
  }
  return response.json() as Promise<BackendHealth>;
}
