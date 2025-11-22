from flask import Flask, render_template, send_file, Response, request
from flask_cors import CORS
from datetime import datetime
import pandas as pd
import io
import json

app = Flask(__name__)
CORS(app)

# --- Función para JSON en UTF-8 ---
def json_utf8(data, status=200):
    return Response(
        json.dumps(data, ensure_ascii=False),
        status=status,
        mimetype="application/json; charset=utf-8"
    )

# Estado inicial de kioskos
kioskos = {
    "kiosko-1": {"estado": "Libre", "mesero": None, "inicio": None, "fin": None, "ultima_accion": None},
    "kiosko-2": {"estado": "Libre", "mesero": None, "inicio": None, "fin": None, "ultima_accion": None},
    "kiosko-3": {"estado": "Libre", "mesero": None, "inicio": None, "fin": None, "ultima_accion": None},
    "kiosko-4": {"estado": "Libre", "mesero": None, "inicio": None, "fin": None, "ultima_accion": None},
}

# Estado de notificaciones para meseros (ESP32-C3)
notificaciones_meseros = {
    "mesero1": {"notificacion": False, "kiosko": None, "timestamp": None},
    "mesero2": {"notificacion": False, "kiosko": None, "timestamp": None},
    "mesero3": {"notificacion": False, "kiosko": None, "timestamp": None},
}

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/estado")
def estado():
    print(f"[{datetime.now().strftime('%H:%M:%S')}] GET /estado desde {request.remote_addr}")
    return json_utf8(kioskos)

# Nueva ruta: Obtener disponibilidad de todos los meseros
@app.route("/meseros/disponibilidad")
def obtener_disponibilidad_meseros():
    print(f"[{datetime.now().strftime('%H:%M:%S')}] GET /meseros/disponibilidad")
    
    disponibilidad = {}
    
    # Verificar cuáles meseros están ocupados
    for mesero_id in ["mesero1", "mesero2", "mesero3"]:
        ocupado = False
        kiosko_asignado = None
        
        # Buscar si el mesero está atendiendo algún kiosko
        for kiosko_id, info in kioskos.items():
            if info["mesero"] == mesero_id and info["estado"] == "En atención":
                ocupado = True
                kiosko_asignado = kiosko_id
                break
        
        disponibilidad[mesero_id] = {
            "disponible": not ocupado,
            "ocupado": ocupado,
            "kiosko": kiosko_asignado
        }
    
    return json_utf8(disponibilidad)

# Nueva ruta: Estado de notificaciones para meseros (ESP32-C3)
@app.route("/mesero/<mesero>/notificacion")
def obtener_notificacion_mesero(mesero):
    print(f"[{datetime.now().strftime('%H:%M:%S')}] GET /mesero/{mesero}/notificacion")
    if mesero in notificaciones_meseros:
        return json_utf8(notificaciones_meseros[mesero])
    return json_utf8({"error": "Mesero no existe"}, status=404)

# Nueva ruta: Limpiar notificación de mesero
@app.route("/mesero/<mesero>/limpiar")
def limpiar_notificacion_mesero(mesero):
    print(f"[{datetime.now().strftime('%H:%M:%S')}] GET /mesero/{mesero}/limpiar")
    if mesero in notificaciones_meseros:
        notificaciones_meseros[mesero]["notificacion"] = False
        notificaciones_meseros[mesero]["kiosko"] = None
        return json_utf8({"msg": f"Notificación de {mesero} limpiada"})
    return json_utf8({"error": "Mesero no existe"}, status=404)

@app.route("/solicitar/<kiosko>")
def solicitar(kiosko):
    print(f"[{datetime.now().strftime('%H:%M:%S')}] GET /solicitar/{kiosko}")
    if kiosko in kioskos and kioskos[kiosko]["estado"] == "Libre":
        kioskos[kiosko]["estado"] = "Pendiente"
        kioskos[kiosko]["inicio"] = datetime.now().strftime("%H:%M:%S")
        kioskos[kiosko]["ultima_accion"] = "Solicitud enviada"
        return json_utf8({"msg": f"{kiosko} solicitó servicio"})
    return json_utf8({"msg": f"{kiosko} no disponible"}, status=400)

@app.route("/atender/<kiosko>/<mesero>")
def atender(kiosko, mesero):
    print(f"[{datetime.now().strftime('%H:%M:%S')}] GET /atender/{kiosko}/{mesero}")
    if kiosko not in kioskos:
        return json_utf8({"msg": "Kiosko no existe"}, status=400)

    # no permitir que el mesero atienda si ya está en otro kiosko
    for k, info in kioskos.items():
        if info["mesero"] == mesero and info["estado"] == "En atención":
            return json_utf8({"msg": f"{mesero} ya está atendiendo {k}"}, status=400)

    if kiosko in kioskos and kioskos[kiosko]["estado"] in ["Pendiente", "Libre"]:
        # Cambiar estado del kiosko
        kioskos[kiosko]["estado"] = "En atención"
        kioskos[kiosko]["mesero"] = mesero
        if not kioskos[kiosko]["inicio"]:
            kioskos[kiosko]["inicio"] = datetime.now().strftime("%H:%M:%S")
        kioskos[kiosko]["ultima_accion"] = f"Atendido por {mesero}"
        
        #  ENVIAR NOTIFICACIÓN AL MESERO (ESP32-C3)
        if mesero in notificaciones_meseros:
            notificaciones_meseros[mesero]["notificacion"] = True
            notificaciones_meseros[mesero]["kiosko"] = kiosko
            notificaciones_meseros[mesero]["timestamp"] = datetime.now().strftime("%H:%M:%S")
            print(f"[NOTIFICACIÓN] Enviada a {mesero} para {kiosko}")
        
        return json_utf8({"msg": f"{kiosko} está siendo atendido por {mesero}"})
    return json_utf8({"msg": f"{kiosko} no puede ser atendido"}, status=400)

# Nueva ruta: Confirmar atención con RFID
@app.route("/confirmar/<kiosko>/<mesero>")
def confirmar_atencion(kiosko, mesero):
    print(f"[{datetime.now().strftime('%H:%M:%S')}] GET /confirmar/{kiosko}/{mesero}")
    
    if kiosko not in kioskos:
        return json_utf8({"msg": "Kiosko no existe"}, status=400)
    
    # Verificar que el mesero asignado sea el correcto
    if kioskos[kiosko]["mesero"] != mesero:
        return json_utf8({"msg": f"Mesero incorrecto. {kiosko} está asignado a {kioskos[kiosko]['mesero']}"}, status=400)
    
    # Confirmar que está en atención
    if kioskos[kiosko]["estado"] != "En atención":
        return json_utf8({"msg": f"{kiosko} no está en atención"}, status=400)
    
    #  LIMPIAR NOTIFICACIÓN DEL MESERO
    if mesero in notificaciones_meseros:
        notificaciones_meseros[mesero]["notificacion"] = False
        notificaciones_meseros[mesero]["kiosko"] = None
        print(f"[CONFIRMACIÓN] {mesero} confirmó atención en {kiosko}")
    
    kioskos[kiosko]["ultima_accion"] = f"Confirmado por {mesero} con RFID"
    
    return json_utf8({"msg": f"{mesero} confirmó atención en {kiosko}"})

@app.route("/finalizar/<kiosko>")
def finalizar(kiosko):
    print(f"[{datetime.now().strftime('%H:%M:%S')}] GET /finalizar/{kiosko}")
    if kiosko not in kioskos:
        return json_utf8({"msg": "Kiosko no existe"}, status=400)

    if kioskos[kiosko]["estado"] == "Libre":
        return json_utf8({"msg": f"{kiosko} está libre, no necesita finalizar"}, status=400)

    # Limpiar notificación del mesero si existe
    mesero = kioskos[kiosko]["mesero"]
    if mesero and mesero in notificaciones_meseros:
        notificaciones_meseros[mesero]["notificacion"] = False
        notificaciones_meseros[mesero]["kiosko"] = None

    kioskos[kiosko]["fin"] = datetime.now().strftime("%H:%M:%S")
    kioskos[kiosko]["ultima_accion"] = "Servicio finalizado"
    kioskos[kiosko]["estado"] = "Libre"
    kioskos[kiosko]["mesero"] = None
    kioskos[kiosko]["inicio"] = None
    return json_utf8({"msg": f"{kiosko} finalizado y quedó libre"})

@app.route("/cancelar/<kiosko>")
def cancelar(kiosko):
    print(f"[{datetime.now().strftime('%H:%M:%S')}] GET /cancelar/{kiosko}")
    if kiosko not in kioskos:
        return json_utf8({"msg": "Kiosko no existe"}, status=400)

    if kioskos[kiosko]["estado"] not in ["Pendiente", "En atención"]:
        return json_utf8({"msg": f"{kiosko} no está en estado cancelable"}, status=400)

    # Limpiar notificación del mesero si existe
    mesero = kioskos[kiosko]["mesero"]
    if mesero and mesero in notificaciones_meseros:
        notificaciones_meseros[mesero]["notificacion"] = False
        notificaciones_meseros[mesero]["kiosko"] = None

    kioskos[kiosko]["fin"] = None
    kioskos[kiosko]["ultima_accion"] = "Servicio cancelado"
    kioskos[kiosko]["estado"] = "Libre"
    kioskos[kiosko]["mesero"] = None
    kioskos[kiosko]["inicio"] = None
    return json_utf8({"msg": f"{kiosko} fue cancelado y quedó libre"})

@app.route("/exportar")
def exportar():
    # --- Hoja 1: Historial ---
    df_historial = pd.DataFrame.from_dict(kioskos, orient="index")
    df_historial.reset_index(inplace=True)
    df_historial.rename(columns={"index": "Kiosko"}, inplace=True)

    # --- Hoja 2: Resumen de Meseros ---
    meseros = df_historial["mesero"].dropna()
    resumen = meseros.value_counts().reset_index()
    resumen.columns = ["Mesero", "Cantidad de servicios"]

    # --- Calcular tiempos de atención ---
    tiempos = []
    for _, row in df_historial.iterrows():
        if row["inicio"] and row["fin"] and row["mesero"]:
            try:
                inicio = datetime.strptime(row["inicio"], "%H:%M:%S")
                fin = datetime.strptime(row["fin"], "%H:%M:%S")
                duracion = (fin - inicio).seconds
                tiempos.append((row["mesero"], duracion))
            except:
                pass
    df_tiempos = pd.DataFrame(tiempos, columns=["Mesero", "Duración(seg)"])
    if not df_tiempos.empty:
        promedio = df_tiempos.groupby("Mesero")["Duración(seg)"].mean().reset_index()
        resumen = pd.merge(resumen, promedio, on="Mesero", how="left")

    # --- Generar Excel en memoria ---
    output = io.BytesIO()
    with pd.ExcelWriter(output, engine="xlsxwriter") as writer:
        df_historial.to_excel(writer, sheet_name="Historial", index=False)
        resumen.to_excel(writer, sheet_name="Resumen Meseros", index=False)

        workbook  = writer.book
        worksheet = workbook.add_worksheet("Gráficos")
        writer.sheets["Gráficos"] = worksheet

        resumen.to_excel(writer, sheet_name="Gráficos", startrow=0, index=False)

        if not resumen.empty:
            chart1 = workbook.add_chart({"type": "column"})
            chart1.add_series({
                "name": "Atenciones",
                "categories": ["Gráficos", 1, 0, len(resumen), 0],
                "values": ["Gráficos", 1, 1, len(resumen), 1],
            })
            chart1.set_title({"name": "Atenciones por Mesero"})
            chart1.set_x_axis({"name": "Mesero"})
            chart1.set_y_axis({"name": "Cantidad"})
            worksheet.insert_chart("E2", chart1)

            chart2 = workbook.add_chart({"type": "pie"})
            chart2.add_series({
                "name": "Distribución de Servicios",
                "categories": ["Gráficos", 1, 0, len(resumen), 0],
                "values": ["Gráficos", 1, 1, len(resumen), 1],
            })
            chart2.set_title({"name": "Distribución porcentual"})
            worksheet.insert_chart("E20", chart2)

    output.seek(0)

    return send_file(
        output,
        as_attachment=True,
        download_name=f"reporte_kioskos_{datetime.now().strftime('%Y%m%d_%H%M%S')}.xlsx",
        mimetype="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
    )

if __name__ == "__main__":
    import socket
    hostname = socket.gethostname()
    local_ip = socket.gethostbyname(hostname)
    
    print("="*60)
    print(" SERVIDOR FLASK - SISTEMA DE KIOSKOS LURUACO")
    print("="*60)
    print(f" Host: 0.0.0.0")
    print(f" Puerto: 5000")
    print(f" IP Local: {local_ip}")
    print(f" Acceso Web: http://{local_ip}:5000")
    print(f" API Estado: http://{local_ip}:5000/estado")
    print("="*60)
    print(f"  USA ESTA IP EN TUS ESP32: {local_ip}")
    print("="*60)
    print(" NUEVAS RUTAS PARA NOTIFICACIONES:")
    print(f"   GET /mesero/<mesero>/notificacion  - Consultar notificación")
    print(f"   GET /confirmar/<kiosko>/<mesero>   - Confirmar atención RFID")
    print("="*60)
    print("Presiona CTRL+C para detener el servidor")
    print("="*60)
    
    app.run(host="0.0.0.0", port=5000, debug=True, threaded=True)