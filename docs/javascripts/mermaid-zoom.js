'use strict';

(function () {

  function attachZoomPan(svg) {
    if (svg.dataset.zoom) return;
    svg.dataset.zoom = '1';

    // Wrap the <pre class="mermaid"> itself so we own a clean div — no Material
    // CSS fights. Setting overflow:hidden on the <pre> loses to Material !important.
    var pre = svg.closest('.mermaid') || svg.parentNode;
    var wrapper = document.createElement('div');
    wrapper.style.cssText =
      'position:relative;overflow:hidden;cursor:grab;' +
      'border:1px solid rgba(0,0,0,0.08);border-radius:4px;';
    pre.parentNode.insertBefore(wrapper, pre);
    wrapper.appendChild(pre);

    // Let the <pre> overflow freely — clipping is handled by the wrapper.
    pre.style.overflow = 'visible';
    pre.style.margin = '0';

    svg.style.transformOrigin = '0 0';
    svg.style.display = 'block';
    svg.style.userSelect = 'none';

    var scale = 1, tx = 0, ty = 0;
    var dragging = false, startX = 0, startY = 0, startTx = 0, startTy = 0;

    function applyTransform() {
      svg.style.transform =
        'translate(' + tx + 'px,' + ty + 'px) scale(' + scale + ')';
    }

    function zoomAt(mx, my, factor) {
      scale = Math.min(Math.max(scale * factor, 0.2), 5);
      tx = mx - (mx - tx) * factor;
      ty = my - (my - ty) * factor;
      applyTransform();
    }

    function zoomCenter(factor) {
      zoomAt(wrapper.clientWidth / 2, wrapper.clientHeight / 2, factor);
    }

    // --- +/−/↺ buttons ---
    var controls = document.createElement('div');
    controls.style.cssText =
      'position:absolute;top:6px;right:6px;z-index:10;' +
      'display:flex;flex-direction:column;gap:3px;';

    function makeBtn(label, title, fn) {
      var b = document.createElement('button');
      b.textContent = label;
      b.title = title;
      b.style.cssText =
        'width:28px;height:28px;padding:0;font-size:15px;line-height:1;' +
        'border:1px solid rgba(0,0,0,0.25);border-radius:4px;' +
        'background:rgba(255,255,255,0.92);cursor:pointer;' +
        'box-shadow:0 1px 3px rgba(0,0,0,0.18);';
      b.addEventListener('click', function (e) {
        e.stopPropagation();
        e.preventDefault();
        fn();
      });
      return b;
    }

    controls.appendChild(makeBtn('+', 'Zoom in',
      function () { zoomCenter(1.25); }));
    controls.appendChild(makeBtn('\u2212', 'Zoom out',
      function () { zoomCenter(1 / 1.25); }));
    controls.appendChild(makeBtn('\u21ba', 'Reset',
      function () { scale = 1; tx = 0; ty = 0; applyTransform(); }));
    wrapper.appendChild(controls);

    // --- Wheel zoom ---
    wrapper.addEventListener('wheel', function (e) {
      e.preventDefault();
      var rect = wrapper.getBoundingClientRect();
      zoomAt(e.clientX - rect.left, e.clientY - rect.top,
             e.deltaY < 0 ? 1.1 : 1 / 1.1);
    }, { passive: false });

    // --- Drag to pan ---
    wrapper.addEventListener('pointerdown', function (e) {
      if (e.target.tagName === 'BUTTON') return;
      dragging = true;
      startX = e.clientX; startY = e.clientY;
      startTx = tx; startTy = ty;
      wrapper.setPointerCapture(e.pointerId);
      wrapper.style.cursor = 'grabbing';
    });

    wrapper.addEventListener('pointermove', function (e) {
      if (!dragging) return;
      tx = startTx + (e.clientX - startX);
      ty = startTy + (e.clientY - startY);
      applyTransform();
    });

    wrapper.addEventListener('pointerup', function () {
      dragging = false;
      wrapper.style.cursor = 'grab';
    });

    wrapper.addEventListener('pointercancel', function () {
      dragging = false;
      wrapper.style.cursor = 'grab';
    });

    // --- Double-click reset ---
    wrapper.addEventListener('dblclick', function (e) {
      if (e.target.tagName === 'BUTTON') return;
      scale = 1; tx = 0; ty = 0;
      applyTransform();
    });
  }

  function initMermaidZoom() {
    document.querySelectorAll('.mermaid svg').forEach(attachZoomPan);
  }

  // Debounced MutationObserver — catches diagrams rendered after SPA navigation.
  var timer = null;
  new MutationObserver(function () {
    clearTimeout(timer);
    timer = setTimeout(initMermaidZoom, 200);
  }).observe(document.body, { childList: true, subtree: true });

  document.addEventListener('DOMContentLoaded', function () {
    // Material's mermaid pass is async; 500 ms covers typical render time.
    setTimeout(initMermaidZoom, 500);
  });

}());
