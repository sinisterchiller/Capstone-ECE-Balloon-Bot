// Generic placeholder used for pages not yet built in the current step.

export function renderPlaceholder(root, title) {
  root.innerHTML = `
    <section class="panel empty">
      <h2 class="panel-title">${title}</h2>
      <p class="empty-text">This page ships in a later build step.</p>
    </section>`;
  return null;
}
