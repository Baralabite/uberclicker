// Reference rates: https://frankfurter.dev/. Checkout remains in AUD.
const regions = {
  AUD: 'AU CX CC KI NR TV', NZD: 'NZ CK NU PN TK', USD: 'US AS EC GU MH FM MP PW PR SV TC TL UM VG VI',
  EUR: 'AD AT AX BE BG BL CY DE EE ES FI FR GF GP GR HR IE IT LT LU LV MC MF MQ MT NL PM PT RE SI SK SM TF VA YT',
  GBP: 'GB GG IM JE', CAD: 'CA', CHF: 'CH LI', JPY: 'JP', CNY: 'CN', HKD: 'HK', SGD: 'SG',
  INR: 'IN', IDR: 'ID', MYR: 'MY', PHP: 'PH', THB: 'TH', VND: 'VN', KRW: 'KR', TWD: 'TW',
  AED: 'AE', SAR: 'SA', ILS: 'IL', TRY: 'TR', ZAR: 'ZA', BRL: 'BR', MXN: 'MX',
  DKK: 'DK FO GL', NOK: 'NO SJ', SEK: 'SE', PLN: 'PL', CZK: 'CZ', HUF: 'HU', RON: 'RO',
  ISK: 'IS', ARS: 'AR', CLP: 'CL', COP: 'CO', PEN: 'PE', UAH: 'UA',
  BDT: 'BD', PKR: 'PK', LKR: 'LK', NPR: 'NP', EGP: 'EG', NGN: 'NG', KES: 'KE', FJD: 'FJ'
};

export function localCurrency(country, languages = []) {
  const match = region => Object.keys(regions).find(code => regions[code].split(' ').includes(region));
  if (country && match(country)) return match(country);
  for (const language of languages) {
    try {
      const code = match(new Intl.Locale(language).region);
      if (code) return code;
    } catch { /* Ignore malformed or regionless browser locales. */ }
  }
  return 'AUD';
}

export async function initPricing(document, navigator, fetchRate = fetch) {
  const select = document.querySelector('#price-currency');
  const amount = document.querySelector('#annual-price');
  const codeLabel = document.querySelector('#price-code');
  const note = document.querySelector('#exchange-note');
  const display = document.querySelector('#price-display');
  let revision = 0;
  const names = new Intl.DisplayNames(navigator.languages, { type: 'currency' });
  for (const code of Object.keys(regions).sort()) {
    const option = document.createElement('option');
    option.value = code;
    option.textContent = `${code} — ${names.of(code)}`;
    select.append(option);
  }
  document.querySelector('#currency-picker').hidden = false;

  function render(currency, rate, message) {
    amount.textContent = new Intl.NumberFormat(navigator.languages, {
      style: 'currency', currency, currencyDisplay: 'narrowSymbol',
      minimumFractionDigits: currency === 'AUD' ? 0 : undefined
    }).format(25 * rate);
    codeLabel.textContent = currency;
    note.textContent = message;
  }

  async function update() {
    const current = ++revision;
    const currency = select.value;
    render('AUD', 1, currency === 'AUD' ? 'Billed in AUD.' : `Loading ${currency} estimate… Showing AUD.`);
    display.setAttribute('aria-busy', String(currency !== 'AUD'));
    if (currency === 'AUD') return;
    try {
      const response = await fetchRate(`https://api.frankfurter.dev/v2/rate/AUD/${currency}`, { signal: AbortSignal.timeout(5000) });
      if (!response.ok) throw new Error('Rate unavailable');
      const data = await response.json();
      if (data.base !== 'AUD' || data.quote !== currency || !Number.isFinite(data.rate) || data.rate <= 0 || !/^\d{4}-\d{2}-\d{2}$/.test(data.date)) throw new Error('Invalid rate');
      if (current !== revision) return;
      render(currency, data.rate, `Estimate at ${data.date} exchange rate. Billed at AUD $25/year; your bank’s rate may vary.`);
    } catch {
      if (current === revision) render('AUD', 1, `Exchange rate unavailable for ${currency}. Showing AUD; billed at AUD $25/year.`);
    } finally {
      if (current === revision) display.setAttribute('aria-busy', 'false');
    }
  }

  select.addEventListener('change', update);
  select.value = localCurrency(null, navigator.languages);
  // Cloudflare provides the visitor's country; static mirrors use the browser region.
  try {
    const response = await fetchRate('/api/country', { signal: AbortSignal.timeout(2000) });
    if (response.ok) {
      const data = await response.json();
      if (revision === 0) select.value = localCurrency(data.country, navigator.languages);
    }
  } catch { /* Browser-region selection already works without the Worker. */ }
  if (revision === 0) await update();
}

if (typeof document !== 'undefined') void initPricing(document, navigator);
