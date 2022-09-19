/*==============================================================================

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Csaba Pinter, PerkLab, Queen's University
  and was supported through the Applied Cancer Research Unit program of Cancer Care
  Ontario with funds provided by the Ontario Ministry of Health and Long-Term Care

==============================================================================*/

#ifndef __qSlicerSubjectHierarchySegmentsPlugin_h
#define __qSlicerSubjectHierarchySegmentsPlugin_h

// Subject Hierarchy includes
#include "qSlicerSubjectHierarchyAbstractPlugin.h"

#include "qSlicerSegmentationsSubjectHierarchyPluginsExport.h"

class qSlicerSubjectHierarchySegmentsPluginPrivate;
class vtkMRMLNode;
class vtkMRMLSubjectHierarchyNode;

/// \ingroup SlicerRt_QtModules_Segmentations
class Q_SLICER_SEGMENTATIONS_PLUGINS_EXPORT qSlicerSubjectHierarchySegmentsPlugin : public qSlicerSubjectHierarchyAbstractPlugin
{
public:
  Q_OBJECT

public:
  typedef qSlicerSubjectHierarchyAbstractPlugin Superclass;
  qSlicerSubjectHierarchySegmentsPlugin(QObject* parent = nullptr);
  ~qSlicerSubjectHierarchySegmentsPlugin() override;

public:
  /// Determines if a subject hierarchy item can be reparented in the hierarchy using the current plugin,
  /// and gets a confidence value for the reparented item.
  /// Most plugins do not perform steps additional to the default, so the default implementation returns a 0
  /// confidence value, which can be overridden in plugins that do handle special cases.
  /// \param itemID Item to be reparented in the hierarchy
  /// \param parentItemID Prospective parent of the item to reparent.
  /// \return Floating point confidence number between 0 and 1, where 0 means that the plugin cannot handle the
  ///   item, and 1 means that the plugin is the only one that can handle the item
  double canReparentItemInsideSubjectHierarchy(vtkIdType itemID, vtkIdType parentItemID)const override;

  /// Reparent an item that was already in the subject hierarchy under a new parent.
  /// \return True if reparented successfully, false otherwise
  bool reparentItemInsideSubjectHierarchy(vtkIdType itemID, vtkIdType parentItemID) override;

  /// Determines if the actual plugin can handle a subject hierarchy item. The plugin with
  /// the highest confidence number will "own" the item in the subject hierarchy (set icon, tooltip,
  /// set context menu etc.)
  /// \param item Item to handle in the subject hierarchy tree
  /// \return Floating point confidence number between 0 and 1, where 0 means that the plugin cannot handle the
  ///   item, and 1 means that the plugin is the only one that can handle the item (by node type or identifier attribute)
  double canOwnSubjectHierarchyItem(vtkIdType itemID)const override;

  /// Get role that the plugin assigns to the subject hierarchy item.
  ///   Each plugin should provide only one role.
  Q_INVOKABLE const QString roleForPlugin()const override;

  /// Get help text for plugin to be added in subject hierarchy module widget help box
  const QString helpText()const override;

  /// Get icon of an owned subject hierarchy item
  /// \return Icon to set, empty icon if nothing to set
  QIcon icon(vtkIdType itemID) override;

  /// Get visibility icon for a visibility state
  QIcon visibilityIcon(int visible) override;

  /// Open module belonging to item and set inputs in opened module
  void editProperties(vtkIdType itemID) override;

  /// Generate tooltip for a owned subject hierarchy item
  QString tooltip(vtkIdType itemID)const override;

  /// Set display visibility of a owned subject hierarchy item
  void setDisplayVisibility(vtkIdType itemID, int visible) override;

  /// Get display visibility of a owned subject hierarchy item
  /// \return Display visibility (0: hidden, 1: shown, 2: partially shown)
  int getDisplayVisibility(vtkIdType itemID)const override;

  /// Set display color of an owned subject hierarchy item
  /// \param color Display color to set
  /// \param terminologyMetaData Map containing terminology meta data. Contents:
  ///   qSlicerTerminologyItemDelegate::TerminologyRole        : string
  ///   qSlicerTerminologyItemDelegate::NameRole               : string
  ///   qSlicerTerminologyItemDelegate::NameAutoGeneratedRole  : bool
  ///   qSlicerTerminologyItemDelegate::ColorAutoGeneratedRole : bool
  void setDisplayColor(vtkIdType itemID, QColor color, QMap<int, QVariant> terminologyMetaData) override;

  /// Get display color of an owned subject hierarchy item
  /// \param terminologyMetaData Output map containing terminology meta data. Contents:
  ///   qSlicerTerminologyItemDelegate::TerminologyRole        : string
  ///   qSlicerTerminologyItemDelegate::NameRole               : string
  ///   qSlicerTerminologyItemDelegate::NameAutoGeneratedRole  : bool
  ///   qSlicerTerminologyItemDelegate::ColorAutoGeneratedRole : bool
  QColor getDisplayColor(vtkIdType itemID, QMap<int, QVariant> &terminologyMetaData)const override;

  /// Get item context menu item actions to add to tree view
  QList<QAction*> itemContextMenuActions()const override;

  /// Show context menu actions valid for a given subject hierarchy item.
  /// \param itemID Subject Hierarchy item to show the context menu items for
  void showContextMenuActionsForItem(vtkIdType itemID) override;

  /// Get visibility context menu item actions to add to tree view.
  /// These item visibility context menu actions can be shown in the implementations of \sa showVisibilityContextMenuActionsForItem
  QList<QAction*> visibilityContextMenuActions()const override;

  /// Show visibility context menu actions valid for a given subject hierarchy item.
  /// \param itemID Subject Hierarchy item to show the visibility context menu items for
  void showVisibilityContextMenuActionsForItem(vtkIdType itemID) override;

  /// Show a segment item in a selected view.
  /// Overridden here to create closed surface representation for display in 3D views.
  /// Returns true on success.
  /// Implemented by calling the segmentation plugin.
  bool showItemInView(vtkIdType itemID, vtkMRMLAbstractViewNode* viewNode, vtkIdList* allItemsToShow) override;

  /// TODO
  int componentIndex(vtkIdType itemID) override;

protected slots:
  /// Show only selected segment, hide all others in segmentation
  void showOnlyCurrentSegment();

  /// Show all segments in segmentation
  void showAllSegments();

  /// Jump to slices in slice views to show current segment
  void jumpSlices();

  /// Clone the selected segment
  void cloneSegment();

protected:
  QScopedPointer<qSlicerSubjectHierarchySegmentsPluginPrivate> d_ptr;

private:
  Q_DECLARE_PRIVATE(qSlicerSubjectHierarchySegmentsPlugin);
  Q_DISABLE_COPY(qSlicerSubjectHierarchySegmentsPlugin);
};

#endif
