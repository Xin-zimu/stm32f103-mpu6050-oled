import * as THREE from "three";

const canvas = document.querySelector("#scene");
const statusEl = document.querySelector("#status");
const lineEl = document.querySelector("#lastLine");
const rollEl = document.querySelector("#roll");
const pitchEl = document.querySelector("#pitch");
const yawEl = document.querySelector("#yaw");

const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setClearColor(0xf6f3ed, 1);

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 100);
camera.position.set(4.2, 3.2, 5.5);
camera.lookAt(0, 0, 0);

const hemi = new THREE.HemisphereLight(0xffffff, 0xb8b0a4, 2.0);
scene.add(hemi);

const key = new THREE.DirectionalLight(0xffffff, 2.3);
key.position.set(4, 5, 6);
scene.add(key);

const fill = new THREE.DirectionalLight(0xf0d7a0, 1.1);
fill.position.set(-5, 2, -3);
scene.add(fill);

const grid = new THREE.GridHelper(7, 14, 0x8d8377, 0xd6cec3);
grid.position.y = -1.4;
scene.add(grid);

const attitudeGroup = new THREE.Group();
scene.add(attitudeGroup);

const bodyMaterials = [
  new THREE.MeshStandardMaterial({ color: 0x2c7a7b, roughness: 0.46, metalness: 0.05 }),
  new THREE.MeshStandardMaterial({ color: 0x2c7a7b, roughness: 0.46, metalness: 0.05 }),
  new THREE.MeshStandardMaterial({ color: 0xf2b84b, roughness: 0.55, metalness: 0.04 }),
  new THREE.MeshStandardMaterial({ color: 0xd9a441, roughness: 0.55, metalness: 0.04 }),
  new THREE.MeshStandardMaterial({ color: 0x3f8f63, roughness: 0.48, metalness: 0.04 }),
  new THREE.MeshStandardMaterial({ color: 0x9b4d3f, roughness: 0.48, metalness: 0.04 }),
];

const body = new THREE.Mesh(new THREE.BoxGeometry(2.7, 0.38, 1.35), bodyMaterials);
body.castShadow = true;
body.receiveShadow = true;
attitudeGroup.add(body);

const nose = new THREE.Mesh(
  new THREE.ConeGeometry(0.22, 0.48, 32),
  new THREE.MeshStandardMaterial({ color: 0x202124, roughness: 0.4 })
);
nose.rotation.z = -Math.PI / 2;
nose.position.x = 1.58;
attitudeGroup.add(nose);

const boardLine = new THREE.LineSegments(
  new THREE.EdgesGeometry(new THREE.BoxGeometry(2.72, 0.4, 1.37)),
  new THREE.LineBasicMaterial({ color: 0x202124, transparent: true, opacity: 0.46 })
);
attitudeGroup.add(boardLine);

const axes = new THREE.AxesHelper(2.4);
axes.position.set(-2.8, -1.05, -1.8);
scene.add(axes);

const target = { roll: 0, pitch: 0, yaw: 0 };
const current = { roll: 0, pitch: 0, yaw: 0 };

function resize() {
  const width = window.innerWidth;
  const height = window.innerHeight;
  renderer.setSize(width, height, false);
  camera.aspect = width / height;
  camera.updateProjectionMatrix();
}

window.addEventListener("resize", resize);
resize();

function updateStatus(data) {
  const status = data.status || "Disconnected";
  statusEl.textContent = status;
  statusEl.className = "status";

  if (status === "Connected") {
    statusEl.classList.add("connected");
  } else if (status === "Demo") {
    statusEl.classList.add("demo");
  } else if (status === "Calibrating") {
    statusEl.classList.add("calibrating");
  } else if (status === "MPU Offline") {
    statusEl.classList.add("error");
  } else if (status === "Data Error") {
    statusEl.classList.add("error");
  }

  if (data.last_line) {
    lineEl.textContent = data.last_line;
  } else if (data.message) {
    lineEl.textContent = data.message;
  }
}

function setNumbers(data) {
  rollEl.textContent = Number(data.roll || 0).toFixed(2);
  pitchEl.textContent = Number(data.pitch || 0).toFixed(2);
  yawEl.textContent = Number(data.yaw || 0).toFixed(2);
}

function connectEvents() {
  const events = new EventSource("/events");

  events.onmessage = (event) => {
    const data = JSON.parse(event.data);
    target.roll = Number(data.roll || 0);
    target.pitch = Number(data.pitch || 0);
    target.yaw = Number(data.yaw || 0);
    updateStatus(data);
    setNumbers(data);
  };

  events.onerror = () => {
    statusEl.textContent = "Disconnected";
    statusEl.className = "status error";
    lineEl.textContent = "Local data stream disconnected";
  };
}

connectEvents();

function lerpAngle(a, b, factor) {
  let delta = b - a;
  while (delta > 180) delta -= 360;
  while (delta < -180) delta += 360;
  return a + delta * factor;
}

function animate() {
  current.roll = lerpAngle(current.roll, target.roll, 0.18);
  current.pitch = lerpAngle(current.pitch, target.pitch, 0.18);
  current.yaw = lerpAngle(current.yaw, target.yaw, 0.18);

  attitudeGroup.rotation.order = "YXZ";
  attitudeGroup.rotation.x = THREE.MathUtils.degToRad(current.pitch);
  attitudeGroup.rotation.y = THREE.MathUtils.degToRad(current.yaw);
  attitudeGroup.rotation.z = THREE.MathUtils.degToRad(-current.roll);

  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}

animate();
