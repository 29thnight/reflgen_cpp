using System.ComponentModel.Composition;
using System;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Language.Intellisense.AsyncCompletion;
using Microsoft.VisualStudio.Text.Editor;
using Microsoft.VisualStudio.Utilities;

namespace Reflgen.VisualStudio.Completion
{
    // C++ 편집기의 자동완성에 [[scope:: 뒤의 attribute 를 더한다. C++ IntelliSense 의 제안과 한 목록에 섞인다.
    [Export(typeof(IAsyncCompletionSourceProvider))]
    [Name("reflgen attribute completion")]
    [ContentType("C/C++")]
    internal sealed class AttributeCompletionSourceProvider : IAsyncCompletionSourceProvider
    {
        [Import] internal ITextDocumentFactoryService Documents = null!;
        [Import(typeof(SVsServiceProvider))] internal IServiceProvider Services = null!;
        public IAsyncCompletionSource GetOrCreate(ITextView textView) =>
            textView.Properties.GetOrCreateSingletonProperty(() => new AttributeCompletionSource(() =>
            {
                ThreadHelper.ThrowIfNotOnUIThread();
                return Documents.TryGetTextDocument(textView.TextBuffer, out ITextDocument document)
                    ? new ProjectBridge(Services).LanguageVersion(document.FilePath) : 20;
            }));
    }
}
