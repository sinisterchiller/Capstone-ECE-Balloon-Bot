// Hash router with page lifecycle: on navigation the previous page's
// cleanup function runs, #page-root is cleared, and the new page renders.

const PAGES = [
  '#/overview',
  '#/gps',
  '#/atmospheric',
  '#/events',
  '#/comm',
  '#/logs',
  '#/settings',
];

export const Router = {
  routes: [],
  current: null,
  _cleanup: null,

  register(routes) {
    this.routes = routes;
  },

  start() {
    const render = () => {
      const hash = location.hash || PAGES[0];
      const route = this.routes.find(r => r.hash === hash) ?? this.routes[0];
      if (!route) return;
      if (this._cleanup) {
        try { this._cleanup(); } catch (err) { console.error('[router] cleanup', err); }
        this._cleanup = null;
      }
      const root = document.getElementById('page-root');
      root.innerHTML = '';
      this.current = route;
      const cleanup = route.render(root);
      this._cleanup = typeof cleanup === 'function' ? cleanup : null;
      document.querySelectorAll('.sidebar nav a').forEach(a => {
        if (a.getAttribute('href') === hash) a.setAttribute('aria-current', 'page');
        else a.removeAttribute('aria-current');
      });
    };
    window.addEventListener('hashchange', render);
    render();
  },
};
