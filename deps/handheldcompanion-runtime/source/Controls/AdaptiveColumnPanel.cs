using System;
using System.Windows;
using System.Windows.Controls;

namespace HandheldCompanion.Controls
{
    /// <summary>
    /// Controls the vertical stacking order when <see cref="AdaptiveColumnPanel"/> collapses
    /// to a single column.
    /// <list type="bullet">
    /// <item><see cref="LastOnTop"/> — last column stacks above earlier columns (default).</item>
    /// <item><see cref="FirstOnTop"/> — first column stays on top; later columns stack below.</item>
    /// </list>
    /// </summary>
    public enum ColumnStackDirection
    {
        /// <summary>Columns render top-to-bottom in their natural (declaration) order.</summary>
        FirstOnTop,
        /// <summary>Columns render in reverse order: the last column appears on top.</summary>
        LastOnTop
    }

    /// <summary>
    /// A panel that distributes its children evenly across <see cref="Columns"/> columns when
    /// the available width is at or above <see cref="BreakWidth"/>, and stacks them
    /// vertically (single column) when narrower.
    /// Children are assigned to columns in round-robin order (child 0 → col 0, child 1 → col 1, …).
    /// Uses the layout-pass constraint width — no ActualWidth feedback loop.
    /// <para>
    /// <see cref="ColumnMaxWidths"/> accepts a comma-separated list of per-column max widths,
    /// e.g. "500,400". Use "Infinity" or leave an entry empty for no cap on that column.
    /// Space freed by capped columns is redistributed to unconstrained columns.
    /// </para>
    /// </summary>
    public class AdaptiveColumnPanel : Panel
    {
        public static readonly DependencyProperty ColumnsProperty =
            DependencyProperty.Register(
                nameof(Columns),
                typeof(int),
                typeof(AdaptiveColumnPanel),
                new FrameworkPropertyMetadata(2,
                    FrameworkPropertyMetadataOptions.AffectsMeasure |
                    FrameworkPropertyMetadataOptions.AffectsArrange,
                    null,
                    (_, v) => Math.Max(1, (int)v)));   // coerce: minimum 1

        public static readonly DependencyProperty BreakWidthProperty =
            DependencyProperty.Register(
                nameof(BreakWidth),
                typeof(double),
                typeof(AdaptiveColumnPanel),
                new FrameworkPropertyMetadata(640.0,
                    FrameworkPropertyMetadataOptions.AffectsMeasure |
                    FrameworkPropertyMetadataOptions.AffectsArrange));

        public static readonly DependencyProperty SpacingProperty =
            DependencyProperty.Register(
                nameof(Spacing),
                typeof(double),
                typeof(AdaptiveColumnPanel),
                new FrameworkPropertyMetadata(8.0,
                    FrameworkPropertyMetadataOptions.AffectsMeasure |
                    FrameworkPropertyMetadataOptions.AffectsArrange));

        /// <summary>
        /// Comma-separated max widths for each column, e.g. "500,400".
        /// Use "Infinity" or omit an entry to leave a column unconstrained.
        /// </summary>
        public static readonly DependencyProperty ColumnMaxWidthsProperty =
            DependencyProperty.Register(
                nameof(ColumnMaxWidths),
                typeof(string),
                typeof(AdaptiveColumnPanel),
                new FrameworkPropertyMetadata(null,
                    FrameworkPropertyMetadataOptions.AffectsMeasure |
                    FrameworkPropertyMetadataOptions.AffectsArrange));

        public int Columns
        {
            get => (int)GetValue(ColumnsProperty);
            set => SetValue(ColumnsProperty, value);
        }

        public double BreakWidth
        {
            get => (double)GetValue(BreakWidthProperty);
            set => SetValue(BreakWidthProperty, value);
        }

        public double Spacing
        {
            get => (double)GetValue(SpacingProperty);
            set => SetValue(SpacingProperty, value);
        }

        /// <summary>
        /// Controls which column appears on top when the panel collapses to a single column.
        /// Default is <see cref="ColumnStackDirection.LastOnTop"/>.
        /// </summary>
        public static readonly DependencyProperty ColumnStackDirectionProperty =
            DependencyProperty.Register(
                nameof(StackDirection),
                typeof(ColumnStackDirection),
                typeof(AdaptiveColumnPanel),
                new FrameworkPropertyMetadata(ColumnStackDirection.FirstOnTop,
                    FrameworkPropertyMetadataOptions.AffectsMeasure |
                    FrameworkPropertyMetadataOptions.AffectsArrange));

        public string ColumnMaxWidths
        {
            get => (string)GetValue(ColumnMaxWidthsProperty);
            set => SetValue(ColumnMaxWidthsProperty, value);
        }

        public ColumnStackDirection StackDirection
        {
            get => (ColumnStackDirection)GetValue(ColumnStackDirectionProperty);
            set => SetValue(ColumnStackDirectionProperty, value);
        }

        // Parses ColumnMaxWidths into a double[] of length `cols`.
        // Missing or empty entries default to double.PositiveInfinity.
        private double[] ParseMaxWidths(int cols)
        {
            var result = new double[cols];
            for (int i = 0; i < cols; i++)
                result[i] = double.PositiveInfinity;

            string spec = ColumnMaxWidths;
            if (string.IsNullOrWhiteSpace(spec))
                return result;

            string[] parts = spec.Split(',');
            for (int i = 0; i < Math.Min(parts.Length, cols); i++)
            {
                string part = parts[i].Trim();
                if (string.IsNullOrEmpty(part) ||
                    part.Equals("Infinity", StringComparison.OrdinalIgnoreCase) ||
                    part == "∞")
                    continue;

                if (double.TryParse(part, System.Globalization.NumberStyles.Any,
                        System.Globalization.CultureInfo.InvariantCulture, out double val) && val > 0)
                    result[i] = val;
            }

            return result;
        }

        // Distributes `available` pixels across `cols` columns respecting per-column max widths.
        // Space freed by capped columns is redistributed to unconstrained ones (iterative).
        private double[] ComputeColumnWidths(double available, int cols)
        {
            double[] maxWidths = ParseMaxWidths(cols);
            double[] widths = new double[cols];
            bool[] capped = new bool[cols];
            double cappedTotal = 0;
            int freeCount = cols;

            // Iteratively cap columns whose equal share exceeds their max.
            for (int pass = 0; pass < cols; pass++)
            {
                double share = freeCount > 0 ? (available - cappedTotal) / freeCount : 0;
                bool anyNewCap = false;
                for (int c = 0; c < cols; c++)
                {
                    if (capped[c]) continue;
                    if (share > maxWidths[c])
                    {
                        widths[c] = maxWidths[c];
                        capped[c] = true;
                        cappedTotal += maxWidths[c];
                        freeCount--;
                        anyNewCap = true;
                    }
                }
                if (!anyNewCap) break;
            }

            // Assign remaining space equally to unconstrained columns.
            double finalShare = freeCount > 0 ? (available - cappedTotal) / freeCount : 0;
            for (int c = 0; c < cols; c++)
            {
                if (!capped[c])
                    widths[c] = Math.Max(0, finalShare);
            }

            return widths;
        }

        protected override Size MeasureOverride(Size availableSize)
        {
            var children = InternalChildren;
            if (children.Count == 0)
                return new Size(0, 0);

            int cols = Columns;
            bool multiCol = !double.IsInfinity(availableSize.Width)
                            && availableSize.Width >= BreakWidth
                            && cols > 1
                            && children.Count > 1;

            if (multiCol)
            {
                double totalSpacing = Spacing * (cols - 1);
                double[] colWidths = ComputeColumnWidths(availableSize.Width - totalSpacing, cols);

                double[] colHeights = new double[cols];
                for (int i = 0; i < children.Count; i++)
                {
                    int col = i % cols;
                    children[i].Measure(new Size(colWidths[col], double.PositiveInfinity));
                    colHeights[col] += children[i].DesiredSize.Height;
                }

                double maxH = 0;
                foreach (double h in colHeights) maxH = Math.Max(maxH, h);
                return new Size(availableSize.Width, maxH);
            }
            else
            {
                double w = double.IsInfinity(availableSize.Width) ? 0 : availableSize.Width;
                double totalH = 0, maxW = 0;
                bool reverse = StackDirection == ColumnStackDirection.LastOnTop;
                int count = children.Count;
                for (int i = 0; i < count; i++)
                {
                    UIElement child = reverse ? children[count - 1 - i] : children[i];
                    child.Measure(new Size(w, double.PositiveInfinity));
                    if (i > 0) totalH += Spacing;
                    totalH += child.DesiredSize.Height;
                    maxW = Math.Max(maxW, child.DesiredSize.Width);
                }
                return new Size(maxW, totalH);
            }
        }

        protected override Size ArrangeOverride(Size finalSize)
        {
            var children = InternalChildren;
            if (children.Count == 0)
                return finalSize;

            int cols = Columns;
            bool multiCol = finalSize.Width >= BreakWidth && cols > 1 && children.Count > 1;

            if (multiCol)
            {
                double totalSpacing = Spacing * (cols - 1);
                double[] colWidths = ComputeColumnWidths(finalSize.Width - totalSpacing, cols);

                // Compute x offsets for each column.
                double[] colX = new double[cols];
                for (int c = 1; c < cols; c++)
                    colX[c] = colX[c - 1] + colWidths[c - 1] + Spacing;

                double[] colY = new double[cols];
                for (int i = 0; i < children.Count; i++)
                {
                    int col = i % cols;
                    double h = children[i].DesiredSize.Height;
                    children[i].Arrange(new Rect(colX[col], colY[col], colWidths[col], h));
                    colY[col] += h;
                }
            }
            else
            {
                double y = 0;
                bool reverse = StackDirection == ColumnStackDirection.LastOnTop;
                int count = children.Count;
                for (int i = 0; i < count; i++)
                {
                    if (i > 0) y += Spacing;
                    UIElement child = reverse ? children[count - 1 - i] : children[i];
                    double h = child.DesiredSize.Height;
                    child.Arrange(new Rect(0, y, finalSize.Width, h));
                    y += h;
                }
            }

            return finalSize;
        }
    }
}
