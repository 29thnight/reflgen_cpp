using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.VisualStudio.Core.Imaging;
using Microsoft.VisualStudio.Imaging;
using Microsoft.VisualStudio.Language.Intellisense.AsyncCompletion;
using Microsoft.VisualStudio.Language.Intellisense.AsyncCompletion.Data;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Adornments;

namespace Reflgen.VisualStudio.Completion
{
    internal sealed class AttributeCompletionSource : IAsyncCompletionSource
    {
        // attribute 목록이 이보다 길게 이어지는 일은 없다 — 커서 앞을 이만큼만 본다.
        private const int LookBehind = 4096;

        private static readonly ImageElement ScopeIcon = Icon(KnownMonikers.Namespace, "Namespace");
        private static readonly ImageElement AttributeIcon = Icon(KnownMonikers.Class, "Attribute");
        private static readonly ImageElement DirectiveIcon = Icon(KnownMonikers.IntellisenseKeyword, "Directive");

        public CompletionStartData InitializeCompletion(CompletionTrigger trigger, SnapshotPoint triggerLocation,
                                                        CancellationToken token)
        {
            if ((trigger.Reason == CompletionTriggerReason.Insertion && !IsTriggerCharacter(trigger.Character)) ||
                !MayBeAttributeName(triggerLocation))
            {
                return CompletionStartData.DoesNotParticipateInCompletion;
            }
            AttributeCompletionContext context = Analyze(triggerLocation);
            if (context.Position == AttributePosition.None)
            {
                return CompletionStartData.DoesNotParticipateInCompletion;
            }
            var span = new SnapshotSpan(triggerLocation - context.PrefixLength, context.PrefixLength);
            return new CompletionStartData(CompletionParticipation.ProvidesItems, span);
        }

        public Task<CompletionContext> GetCompletionContextAsync(IAsyncCompletionSession session,
                                                                 CompletionTrigger trigger,
                                                                 SnapshotPoint triggerLocation,
                                                                 SnapshotSpan applicableToSpan, CancellationToken token)
        {
            AttributeCompletionContext context = Analyze(applicableToSpan.End);
            AttributeCatalog catalog = CatalogStore.Instance.Current;
            var items = new List<CompletionItem>();
            if (context.Position == AttributePosition.Member && context.Scope != null)
            {
                items.AddRange(catalog.InScope(context.Scope).Select(CreateItem));
            }
            else if (context.Position == AttributePosition.Name)
            {
                items.AddRange(catalog.Scopes.Select(CreateScopeItem));
                if (context.Scope != null) // [[using ns: 뒤
                {
                    items.AddRange(catalog.InScope(context.Scope).Select(CreateItem));
                }
            }
            return Task.FromResult(new CompletionContext(items.ToImmutableArray()));
        }

        public Task<object> GetDescriptionAsync(IAsyncCompletionSession session, CompletionItem item,
                                                CancellationToken token)
        {
            object description = item.Properties.TryGetProperty(typeof(AttributeEntry), out AttributeEntry entry)
                                     ? Describe(entry)
                                     : $"attribute namespace '{item.DisplayText}'";
            return Task.FromResult(description);
        }

        private CompletionItem CreateItem(AttributeEntry entry)
        {
            var item = new CompletionItem(entry.Name, this,
                                          entry.Kind == AttributeKind.Directive ? DirectiveIcon : AttributeIcon,
                                          ImmutableArray<CompletionFilter>.Empty, entry.Scope);
            item.Properties.AddProperty(typeof(AttributeEntry), entry);
            return item;
        }

        // "reflgen::" 까지 넣는다 — 이름공간 다음에 올 것은 늘 "::" 다.
        private CompletionItem CreateScopeItem(string scope) =>
            new CompletionItem(scope, this, ScopeIcon, ImmutableArray<CompletionFilter>.Empty, string.Empty,
                               scope + "::", scope, scope, ImmutableArray<ImageElement>.Empty);

        private static string Describe(AttributeEntry entry)
        {
            string signatures = string.Join("\n", entry.Signature.Split('\n').Select(line => entry.Scope + "::" + line));
            return entry.Summary.Length == 0 ? signatures : signatures + "\n\n" + entry.Summary;
        }

        private static AttributeCompletionContext Analyze(SnapshotPoint caret)
        {
            int start = System.Math.Max(0, caret.Position - LookBehind);
            return AttributeContext.Analyze(caret.Snapshot.GetText(start, caret.Position - start));
        }

        // 식별자와 공백을 거슬러 올라가 ':' '[' ',' 가 나오는 자리만 attribute 이름 자리일 수 있다. C/C++ 편집기의
        // 키 입력마다 불리므로 대부분을 앞 텍스트 4KB 를 읽기 전에 여기서 걸러 낸다.
        private static bool MayBeAttributeName(SnapshotPoint caret)
        {
            ITextSnapshot snapshot = caret.Snapshot;
            int position = caret.Position;
            int limit = System.Math.Max(0, position - 256);
            while (position > limit && (char.IsLetterOrDigit(snapshot[position - 1]) || snapshot[position - 1] == '_'))
            {
                --position;
            }
            while (position > limit && char.IsWhiteSpace(snapshot[position - 1]))
            {
                --position;
            }
            if (position == 0)
            {
                return false;
            }
            char before = snapshot[position - 1];
            return before == ':' || before == '[' || before == ',';
        }

        // 식별자를 치거나 "::"·"[["·"," 를 막 쳤을 때만 끼어든다.
        private static bool IsTriggerCharacter(char c) => char.IsLetterOrDigit(c) || c == '_' || c == ':' || c == '[' ||
                                                          c == ',';

        private static ImageElement Icon(Microsoft.VisualStudio.Imaging.Interop.ImageMoniker moniker, string name) =>
            new ImageElement(new ImageId(moniker.Guid, moniker.Id), name);
    }
}
