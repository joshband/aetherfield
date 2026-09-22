(function () {
  "use strict";

  // Reveal on scroll
  var reveals = document.querySelectorAll(".reveal");
  if ("IntersectionObserver" in window && reveals.length) {
    var io = new IntersectionObserver(
      function (entries) {
        entries.forEach(function (entry) {
          if (entry.isIntersecting) {
            entry.target.classList.add("is-visible");
            io.unobserve(entry.target);
          }
        });
      },
      { rootMargin: "0px 0px -8% 0px", threshold: 0.12 }
    );
    reveals.forEach(function (el) {
      io.observe(el);
    });
  } else {
    reveals.forEach(function (el) {
      el.classList.add("is-visible");
    });
  }

  // Homogeneous FDN decay law visualization (ADR-002/003): gamma^n
  var canvas = document.getElementById("decayCanvas");
  if (canvas && canvas.getContext) {
    var ctx = canvas.getContext("2d");
    var dpr = window.devicePixelRatio || 1;

    function resize() {
      var cssW = canvas.clientWidth || 600;
      var cssH = canvas.clientHeight || 160;
      canvas.width = cssW * dpr;
      canvas.height = cssH * dpr;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      return { w: cssW, h: cssH };
    }

    var fs = 48000;
    var T60 = 1.0;
    var gamma = Math.pow(10, -3 / (fs * T60));
    var totalSamples = fs * 1.4;

    function draw(t) {
      var size = resize();
      var w = size.w;
      var h = size.h;
      var mid = h / 2;
      ctx.clearRect(0, 0, w, h);

      ctx.strokeStyle = "rgba(255,255,255,0.06)";
      ctx.lineWidth = 1;
      for (var gy = 0; gy <= 4; gy++) {
        var y = (h / 4) * gy;
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(w, y);
        ctx.stroke();
      }

      ctx.strokeStyle = "rgba(201,149,108,0.55)";
      ctx.setLineDash([3, 4]);
      ctx.beginPath();
      for (var x = 0; x <= w; x++) {
        var n = (x / w) * totalSamples;
        var env = Math.pow(gamma, n);
        var y2 = mid - env * (mid - 10);
        if (x === 0) ctx.moveTo(x, y2);
        else ctx.lineTo(x, y2);
      }
      ctx.stroke();
      ctx.setLineDash([]);

      ctx.strokeStyle = "#6fa8b5";
      ctx.lineWidth = 1.6;
      ctx.beginPath();
      var freq = 5.5;
      for (var x2 = 0; x2 <= w; x2++) {
        var n2 = (x2 / w) * totalSamples;
        var env2 = Math.pow(gamma, n2);
        var phase = (x2 / w) * Math.PI * 2 * freq + t;
        var y3 = mid + Math.sin(phase) * env2 * (mid - 10);
        if (x2 === 0) ctx.moveTo(x2, y3);
        else ctx.lineTo(x2, y3);
      }
      ctx.stroke();
    }

    var reduceMotion =
      window.matchMedia &&
      window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    if (reduceMotion) {
      draw(0);
    } else {
      var start = null;
      function frame(ts) {
        if (!start) start = ts;
        draw((ts - start) / 4000);
        requestAnimationFrame(frame);
      }
      requestAnimationFrame(frame);
    }

    window.addEventListener("resize", function () {
      draw(0);
    });
  }
})();
