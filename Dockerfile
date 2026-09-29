FROM python:3.10-slim
WORKDIR /app/backend
COPY backend/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY index.html style.js /app/
COPY backend/main.py .
EXPOSE 8000
CMD ["uvicorn","main:app","--host","0.0.0.0","--port","8000"]
