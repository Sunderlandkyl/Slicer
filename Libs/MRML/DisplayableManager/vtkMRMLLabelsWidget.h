/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Brigham and Women's Hospital

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

/**
 * @class   vtkMRMLLabelsWidget
 * @brief   Widget for managing label display
 *
 * This class provides the widget infrastructure for label display,
 * managing the label representation.
 *
 * @sa
 * vtkMRMLAbstractWidget vtkMRMLLabelsWidgetRepresentation
 */

#ifndef __vtkMRMLLabelsWidget_h
#define __vtkMRMLLabelsWidget_h

#include "vtkMRMLAbstractWidget.h"
#include "vtkMRMLDisplayableManagerExport.h"

class vtkMRMLLabelsWidgetRepresentation;
class vtkMRMLLabelDisplayNode;

class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLLabelsWidget : public vtkMRMLAbstractWidget
{
public:
  static vtkMRMLLabelsWidget* New();
  vtkTypeMacro(vtkMRMLLabelsWidget, vtkMRMLAbstractWidget);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /// Set the label display node
  virtual void SetLabelDisplayNode(vtkMRMLLabelDisplayNode* displayNode);
  vtkMRMLLabelDisplayNode* GetLabelDisplayNode();

  /// Get the widget representation
  vtkMRMLLabelsWidgetRepresentation* GetLabelsRepresentation();

  /// Create the default widget representation
  void CreateDefaultRepresentation();

  /// Update from renderer (e.g., camera changes)
  virtual void UpdateFromRenderer();

protected:
  vtkMRMLLabelsWidget();
  ~vtkMRMLLabelsWidget() override;

  vtkWeakPointer<vtkMRMLLabelDisplayNode> LabelDisplayNode;

private:
  vtkMRMLLabelsWidget(const vtkMRMLLabelsWidget&) = delete;
  void operator=(const vtkMRMLLabelsWidget&) = delete;
};

#endif
