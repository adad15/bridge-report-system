import asyncio

from httpx import ASGITransport, AsyncClient

from bridge_report_tools.main import app


def test_health_returns_tool_service_identity() -> None:
    async def get_health_response():
        transport = ASGITransport(app=app)
        async with AsyncClient(transport=transport, base_url="http://testserver") as client:
            return await client.get("/health")

    response = asyncio.run(get_health_response())

    assert response.status_code == 200
    assert response.json() == {
        "status": "ok",
        "service": "bridge-report-python-tools",
        "version": "0.1.0",
        "host": "127.0.0.1",
        "port": 18081,
    }
