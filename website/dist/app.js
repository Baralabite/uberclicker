function previewAction(action) {
  document.querySelector('#last-action').textContent = `Last: ${action}`;
  document.querySelector('#last-receipt').textContent = 'Broadcast · simulated';
  document.querySelector('#action-feedback').textContent = `${action} action. Broadcast simulated in this demo.`;
}
document.querySelector('#previous-cue').addEventListener('click', () => previewAction('Previous'));
document.querySelector('#next-cue').addEventListener('click', () => previewAction('Next'));
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
