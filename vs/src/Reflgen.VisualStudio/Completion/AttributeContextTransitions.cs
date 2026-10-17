using System;
using System.ComponentModel.Composition;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.VisualStudio.Threading;
using Microsoft.VisualStudio.Language.Intellisense.AsyncCompletion;
using Microsoft.VisualStudio.Language.Intellisense.AsyncCompletion.Data;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Text.Editor;
using Microsoft.VisualStudio.Utilities;

namespace Reflgen.VisualStudio.Completion
{
    // Completion items are computed once per session. Entering '= ...' or a namespace must
    // start a new session; otherwise the root standard-attribute list remains attached to [[=.
    [Export(typeof(IWpfTextViewCreationListener))]
    [ContentType("C/C++")]
    [TextViewRole(PredefinedTextViewRoles.Editable)]
    internal sealed class AttributeContextTransitions : IWpfTextViewCreationListener
    {
        [Import] internal IAsyncCompletionBroker Broker = null!;
        [Import] internal JoinableTaskContext ThreadingContext = null!;
        public void TextViewCreated(IWpfTextView view)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            var tasks = ThreadingContext.Factory;
            EventHandler changed = (_, __) => tasks.RunAsync(async () =>
            {
                // Run after the type/commit command has advanced the caret.
                await Task.Yield();
                await tasks.SwitchToMainThreadAsync();
                if (view.IsClosed || !view.HasAggregateFocus) return;
                var caret = view.Caret.Position.BufferPosition;
                if (caret.Position == 0) return;
                char last = caret.Snapshot[caret.Position - 1];
                if (last != '=' && last != ':' && last != ',') return;
                int start = Math.Max(0, caret.Position - 4096);
                var context = AttributeContext.Analyze(caret.Snapshot.GetText(start, caret.Position - start));
                if (!CatalogStore.Instance.Current.OwnsCompletion(context)) return;
                if (last == '=' && !context.IsAnnotation) return;
                Broker.GetSession(view)?.Dismiss();
                var trigger = new CompletionTrigger(CompletionTriggerReason.Invoke, caret.Snapshot);
                Broker.TriggerCompletion(view, trigger, caret, CancellationToken.None)?
                    .OpenOrUpdate(trigger, caret, CancellationToken.None);
            }).FileAndForget("reflgen/completion-context");
            view.TextBuffer.PostChanged += changed;
            view.Closed += (_, __) => view.TextBuffer.PostChanged -= changed;
        }
    }
}
