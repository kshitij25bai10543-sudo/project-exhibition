# StormSense AI — demo backend
All data is a DEMO SIMULATION (scripted storm). No real weather data, no trained model.

    pip install -r requirements.txt
    uvicorn main:app --reload        # http://localhost:8000  (site) · /docs (API)

Docker (run from the folder containing index.html):  docker build -f backend/Dockerfile -t stormsense . && docker run -p 8000:8000 stormsense

Endpoints: GET /api/health · /api/storm?hour= · /api/storm/track · /api/forecast/heatmap?hour=
POST /api/alerts/subscribe · GET /api/alerts/subscriptions · GET /api/alerts · DELETE /api/alerts/subscribe/{id}
