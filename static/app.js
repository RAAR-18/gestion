/* ───────────────────────────────
   VARIABLES
─────────────────────────────── */
const btnModo = document.getElementById("btnModo");
const btnExport = document.getElementById("btnExport");
const notiCont = document.getElementById("notificaciones");

let estadosPrevios = {};
let audioHabilitado = false;
let meserosDisponibilidad = {}; // Nuevo: tracking de disponibilidad

/* ───────────────────────────────
   AUDIO DE SOLICITUD
─────────────────────────────── */
let audioSolicitud = new Audio("/static/sonido.mp3");
audioSolicitud.load();

// Chrome exige interacción para permitir audio
document.addEventListener("click", () => {
    if (!audioHabilitado) {
        audioSolicitud.play().then(() => {
            audioSolicitud.pause();
            audioSolicitud.currentTime = 0;
            audioHabilitado = true;
        }).catch(() => {});
    }
});

/* ───────────────────────────────
   MODO OSCURO / CLARO
─────────────────────────────── */
btnModo.addEventListener("click", () => {
    document.body.classList.toggle("night-mode");
    document.body.classList.toggle("day-mode");
    btnModo.textContent = document.body.classList.contains("night-mode") ? "☀" : "🌙";
});

/* ───────────────────────────────
   EXPORTAR
─────────────────────────────── */
btnExport.addEventListener("click", () => {
    window.location.href = "/exportar";
});

/* ───────────────────────────────
   POPUP DE NOTIFICACIÓN
─────────────────────────────── */
function mostrarNotificacion(text, cssClass) {
    const n = document.createElement("div");
    n.className = "notificacion " + (cssClass || "");
    n.innerText = text;
    notiCont.appendChild(n);
    setTimeout(() => n.remove(), 3500);
}

/* ───────────────────────────────
   POPUP DE MESEROS
─────────────────────────────── */
function mostrarPopupMeseros(kiosko) {
    const popup = document.createElement("div");

    popup.style = `
        position: fixed;
        top: 50%; left: 50%;
        transform: translate(-50%,-50%);
        background: white;
        padding: 20px;
        border-radius: 10px;
        z-index: 2000;
        text-align: center;
        box-shadow: 0 0 20px rgba(0,0,0,0.4);
    `;

    // Construir botones dinámicamente según disponibilidad
    let botonesHTML = '';
    for (let i = 1; i <= 3; i++) {
        const meseroId = `mesero${i}`;
        const disponible = meserosDisponibilidad[meseroId]?.disponible !== false;
        const kioskoOcupado = meserosDisponibilidad[meseroId]?.kiosko || '';
        
        const disabledAttr = disponible ? '' : 'disabled';
        const estiloDeshabilitado = disponible ? '' : 'opacity: 0.4; cursor: not-allowed;';
        const textoEstado = disponible ? '' : ` (en ${kioskoOcupado.replace('kiosko-', 'K')})`;
        
        botonesHTML += `
            <button 
                class="btn-accion" 
                onclick="asignarMesero('${kiosko}','${meseroId}')"
                ${disabledAttr}
                style="${estiloDeshabilitado}"
            >
                Mesero ${i}${textoEstado}
            </button>
        `;
    }

    popup.innerHTML = `
        <h3>Asignar mesero a ${kiosko.replace("kiosko-","K")}</h3>
        ${botonesHTML}
        <br><br>
        <button class="btn-accion cancelar" onclick="this.parentElement.remove()">Cerrar</button>
    `;

    document.body.appendChild(popup);
}

/* ───────────────────────────────
   ASIGNAR MESERO
─────────────────────────────── */
async function asignarMesero(kiosko, mesero) {
    // Verificar disponibilidad antes de asignar
    if (meserosDisponibilidad[mesero]?.disponible === false) {
        const kioskoOcupado = meserosDisponibilidad[mesero]?.kiosko || '';
        mostrarNotificacion(
            `${mesero} está ocupado atendiendo ${kioskoOcupado.replace('kiosko-', 'K')}`, 
            "pendiente"
        );
        return;
    }

    const res = await fetch(`/atender/${kiosko}/${mesero}`);
    const data = await res.json();

    if (data.msg.includes("no puede ser atendido") || data.msg.includes("ya está atendiendo")) {
        mostrarNotificacion("Mesero ocupado", "pendiente");
        return;
    }

    mostrarNotificacion(data.msg, "finalizada");
    await actualizarEstado();

    // Cerrar popup
    document.querySelectorAll("div").forEach(div => {
        if (div.innerHTML.includes("Asignar mesero")) div.remove();
    });
}

/* ───────────────────────────────
   ACTUALIZAR DISPONIBILIDAD DE MESEROS
─────────────────────────────── */
async function actualizarDisponibilidadMeseros() {
    try {
        const res = await fetch("/meseros/disponibilidad");
        const data = await res.json();
        meserosDisponibilidad = data;
        
        // Actualizar botones en los kioskos
        actualizarBotonesAsignacion();
    } catch (err) {
        console.error("Error actualizando disponibilidad:", err);
    }
}

/* ───────────────────────────────
   ACTUALIZAR BOTONES DE ASIGNACIÓN
─────────────────────────────── */
function actualizarBotonesAsignacion() {
    document.querySelectorAll('.assign-btn').forEach(btn => {
        const mesero = btn.getAttribute('data-mesero');
        const disponible = meserosDisponibilidad[mesero]?.disponible !== false;
        
        if (disponible) {
            btn.disabled = false;
            btn.style.opacity = '1';
            btn.style.cursor = 'pointer';
            btn.style.background = '#2d7cff';
        } else {
            btn.disabled = true;
            btn.style.opacity = '0.45';
            btn.style.cursor = 'not-allowed';
            btn.style.background = '#999';
        }
    });
}

/* ───────────────────────────────
   ACTUALIZAR ESTADO
─────────────────────────────── */
async function actualizarEstado() {
    try {
        const res = await fetch("/estado");
        const data = await res.json();

        // Actualizar círculos de kiosko
        for (const key of Object.keys(data)) {
            const el = document.getElementById(key);
            if (!el) continue;

            const estado = data[key].estado;
            el.className = "kiosko";

            let claseEstado = "libre";
            if (estado === "Pendiente") claseEstado = "pendiente";
            if (estado === "En atención") claseEstado = "en-atencion";
            if (estado === "Finalizada") claseEstado = "finalizada";

            el.classList.add(claseEstado);

            if (estadosPrevios[key] !== estado) {
                el.classList.add("spin");
                setTimeout(() => el.classList.remove("spin"), 900);

                if (estado === "Pendiente") {
                    mostrarNotificacion(`🔔 ${key} solicitó servicio`, "pendiente");
                    if (audioHabilitado) audioSolicitud.play();
                }
                if (estado === "Finalizada") {
                    mostrarNotificacion(`✔ ${key} finalizó`, "finalizada");
                }
            }
        }

        // Actualizar tabla de kioskos
        const tbody = document.querySelector("#tablaKioskos tbody");
        tbody.innerHTML = "";

        for (const [key, info] of Object.entries(data)) {
            if (info.estado !== "Libre") {
                const tr = document.createElement("tr");
                tr.innerHTML = `
                    <td>${key.replace("kiosko-", "K")}</td>
                    <td>${info.estado}</td>
                    <td>${info.mesero || "-"}</td>
                    <td>${info.ultima_accion || "-"}</td>
                    <td>
                        <button class="btn-accion finalizar" onclick="finalizarKiosko('${key}')">Finalizar</button>
                        <button class="btn-accion cancelar" onclick="cancelarKiosko('${key}')">Cancelar</button>
                        <button class="btn-accion" onclick="mostrarPopupMeseros('${key}')">Asignar Mesero</button>
                    </td>
                `;
                tbody.appendChild(tr);
            }
        }

        estadosPrevios = {};
        for (const k of Object.keys(data)) estadosPrevios[k] = data[k].estado;

        // Actualizar disponibilidad de meseros
        await actualizarDisponibilidadMeseros();

    } catch (err) {
        console.error("Error actualizando:", err);
    }
}

/* ───────────────────────────────
   FINALIZAR
─────────────────────────────── */
async function finalizarKiosko(kiosko) {
    const res = await fetch(`/finalizar/${kiosko}`);
    const data = await res.json();
    mostrarNotificacion(data.msg, "finalizada");
    await actualizarEstado();
}

/* ───────────────────────────────
   CANCELAR
─────────────────────────────── */
async function cancelarKiosko(kiosko) {
    const res = await fetch(`/cancelar/${kiosko}`);
    const data = await res.json();
    mostrarNotificacion(data.msg, "pendiente");
    await actualizarEstado();
}

/* ───────────────────────────────
   AGREGAR EVENT LISTENERS A BOTONES DE ASIGNACIÓN
─────────────────────────────── */
document.addEventListener('DOMContentLoaded', () => {
    document.querySelectorAll('.assign-btn').forEach(btn => {
        btn.addEventListener('click', async () => {
            const kiosko = btn.getAttribute('data-kiosko');
            const mesero = btn.getAttribute('data-mesero');
            await asignarMesero(kiosko, mesero);
        });
    });
});

/* ───────────────────────────────
   LOOP DE ACTUALIZACIÓN
─────────────────────────────── */
setInterval(actualizarEstado, 2500);
window.addEventListener("load", actualizarEstado);