<script>
  let { values = [], color = 'var(--acc)', h = 28, w = 120 } = $props();
  let v = $derived(values.filter(x => x != null && !isNaN(x)));
  let d = $derived.by(() => { if (v.length < 2) return ''; const lo = Math.min(...v), hi = Math.max(...v), r = hi - lo || 1;
    return v.map((x, i) => (i ? 'L' : 'M') + (i / (v.length - 1) * w).toFixed(1) + ',' + (h - 2 - (x - lo) / r * (h - 4)).toFixed(1)).join(''); });
</script>
<svg viewBox="0 0 {w} {h}" width="100%" height={h} preserveAspectRatio="none" class="sp"><path {d} fill="none" stroke={color} stroke-width="1.6" vector-effect="non-scaling-stroke"/></svg>
<style>.sp{display:block}</style>
