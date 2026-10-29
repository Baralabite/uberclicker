function previewAction(action) {
  document.querySelector('#last-action').textContent = `Last: ${action}`;
  document.querySelector('#last-receipt').textContent = 'Acknowledged · simulated';
  document.querySelector('#action-feedback').textContent = `${action} action. Acknowledgement simulated for this preview.`;
}
document.querySelector('#previous-cue').addEventListener('click', () => previewAction('Previous'));
document.querySelector('#next-cue').addEventListener('click', () => previewAction('Next'));
const connectionDetails = {
  ble: 'Pair over authenticated BLE in the native or supported browser app. Cues stay paused until physical setup exits or expires, including after Apply or Cancel.',
  lan: 'LAN configuration is unavailable in current firmware. A device on the same network still needs a protected configuration service; use BLE setup.',
  linked: 'Future option: remote access needs an authenticated cloud relay and an online device. Linking an account alone does not make a device reachable.'
};
document.querySelectorAll('input[name="connection"]').forEach(input => {
  input.addEventListener('change', () => {
    document.querySelector('#connection-detail').textContent = connectionDetails[input.value];
  });
});
const buyCountry = document.querySelector('#buy-country');
function updateBuyLinks() {
  document.querySelectorAll('.buy-options').forEach(list => {
    list.hidden = list.id !== buyCountry.value;
  });
}
buyCountry.addEventListener('change', updateBuyLinks);
updateBuyLinks();

// The informative illustration remains available if modules cannot load.
setTimeout(() => {
  const stage = document.querySelector('#device-viewport');
  if (!stage.classList.contains('loaded') && document.querySelector('#model-status').textContent === 'Loading 3D preview…') {
    document.querySelector('#model-status').textContent = 'Static illustration · 3D unavailable';
  }
}, 7000);
